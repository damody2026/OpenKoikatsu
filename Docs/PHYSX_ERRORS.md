# PhysX 錯誤紀錄（避免重犯）

> 2026-10-15 自 OK_Demo（`E:\PhysX\UE5.8\Samples\OK_Demo`，已退役）移入本專案。路徑與參數已改為本專案的名稱：OK_Demo → OpenKoikatsu、`Scripts/*` → `Tools/*`、`-Demo*` → `-Pair*`。整合計畫見 `Docs/PHYSX_ENGINE_PORT_PLAN.md`。

每條：症狀 → 原因 → 正確做法。

1. **Engine PCH 與 PhysX 標頭衝突**：`physx::PxQuat` 重複定義 → UE Chaos 標頭 forward-declare 同名類別 →
   外掛模組 `PCHUsage = NoPCHs`，只依賴 Core/Projects/Engine，PhysX 標頭放在獨立 .cpp。
2. **`check()` 巨集**：PhysX 的 `check()` 成員函式被 UE 巨集展開 → include 前後 `push_macro/undef/pop_macro("check","verify")`。
3. **PhysXExtensions 連結失敗**：缺 `PxCookTriangleMesh` 等 → Extensions 靜態庫需要 `PhysXCooking.lib/.dll`。
4. **PxPlane 方向**：平面幾何法線是 shape 的 X 軸，`setGlobalPose(平移)` 會把地板變成直立牆 →
   用 `PxTransformFromPlaneEquation`。
5. **剛體睡著**：只改關節驅動目標不會喚醒剛體，布娃娃停在錯誤姿勢 → `setSleepThreshold(0)`。
6. **D6 slerp 驅動無效**：PhysX 5 預設角度驅動模式是 swing/twist，`eSLERP` 被忽略（布娃娃全軟）→
   `setAngularDriveConfig(PxD6AngularDriveConfig::eSLERP)`，且之後要重設驅動。
7. **加速度驅動撐不住身體**：加速度模式的力矩跟子剛體慣量成正比，細膠囊撐不住上半身 →
   用力驅動，剛度依「承重質量」（自身＋所有下游）縮放；慣量設下限。
8. **骨骼旋轉語意**：Unity 的組合 `Transform.rotation` 不含鏡像縮放，和世界矩陣幾何不一致 →
   物理用的骨骼框架一律由世界矩陣取（`bone_frame`），寫回用世界矩陣旋轉再轉回 local。
9. **多餘的 `rebuild_worlds`**：沒寫入骨架時也重建世界矩陣，改變後續動態骨骼的輸入（頭髮爆開）→ 只有真的寫入才重建。
10. **頭髮粒子爆炸**：偶發大位移後關節把粒子拖成長條 → 輸入／輸出檢查：離父節點或目標過遠就重置到動畫位置並重建場景。
11. **身體代理過大**：靜止時貼皮的頭髮／衣服被推開（瀏海蓋臉）→ 碰撞半徑收縮到「動畫姿勢的空隙」。
    ~~第一次見到時快照~~：姿勢改變（抬頭）後快照失效，必須每幀依動畫姿勢計算。
12. **Tools junction**：`OpenKoikatsuEditor.Target.cs` 的 PreBuildSteps 透過 `Tools` junction 在原版目錄跑 cargo →
    改指向 `Tools`。任何 Build.cs／Target.cs 都不得引用 `Tools/ok-extract`。
13. **UBT Log.txt 被覆寫**：其他工作階段同時用同一引擎建置，`Engine/Programs/UnrealBuildTool/Log.txt` 不可信 →
    看自己 `Build.ps1` 的 stdout。
14. **DLL 被鎖**：遊戲還在跑時建置連結失敗 → 建置前先結束 UnrealEditor。
15. **稽核選錯 UI**：互動場景沒有 gallery 的 `PairCombo`，稽核卡住不動 → 用 `UOKInteractionWidget::ReadyMotionCombo`。
16. **bash heredoc 內含 `'''` 的 Python**：工具解析失敗 → Python 腳本先用 Write 寫檔再執行。
17. **布娃娃回授**：（已驗證不是問題）骨架每幀會重新取樣動畫；保留「寫入前姿勢」還原作為保險。
18. **Python 補丁比對縮排**：重構後縮排層數改變，用寫死 tab 數的字串比對會失敗（且前半段不會寫入）→
    先 `grep -n ... | cat -A` 看實際縮排，或直接用 Edit 工具。
19. **布娃娃驅動增益寫死**：固定倍數 × 質量會讓小部位（手）不穩、大部位（軀幹）下垂 →
    依「關節所帶動子樹對關節的轉動慣量」算 K=Iω²、D=2Iω（臨界阻尼），所有關節同一個自然頻率，換卡片體型也通用。
20. **穿模只靠 PhysX 接觸**：接觸是軟的（contact offset、關節與接觸互相拉扯、快速運動的運動學碰撞體），
    稽核量到 1～7 cm 穿入 → 解算後加精確位置投影（先修骨長、最後推出碰撞體、骨段中點也檢查）。
21. **跨角色碰撞假設同一座標系**：兩個角色各有自己的骨架空間 → 每幀由 actor 發佈「骨架空間→世界」矩陣，
    對方身體膠囊經由世界轉到自己的空間；同一矩陣也用來讓 actor 移動時頭髮衣服產生慣性。
22. **空隙只算粒子**：投影與稽核都檢查骨段中點，但收縮半徑只看粒子 → 靜止時中點就在膠囊內，投影持續拉扯（violations 數百）→
    動畫空隙也要包含骨段中點，三者（空隙、投影、稽核）用同一組量測點。
23. **頭髮亂跳**：(a) 推出碰撞體時保留往內速度 → 下一幀又撞進去（彈跳）；(b) 碰撞半徑每幀跳動 → 一幀內把頭髮推開；
    (c) 爆炸保護一次重置整條鏈 → 整束頭髮瞬移；(d) 髮束彼此碰撞 → 相鄰髮束互推。
    → 非彈性接觸（去掉往內速度）、半徑可立即縮小但每幀最多長 1 cm、只修出問題的單顆粒子、鏈對鏈碰撞預設關閉。
    稽核加入 `maxProjection`（單次最大修正）與 `resets` 作為抖動指標。
24. **測試要在頭向後仰時做**：只播動畫看不到最壞情況 → `OKPhysXSecondary.DebugHeadTilt <度>` 在動畫上再把頭往後仰；
    Demo 的「頭看鏡頭」會跟它打架，稽核模式自動關閉看鏡頭。
25. **函式指標宣告多一個括號**：改 Register 簽名時 header 多了 `)` → 改 ABI 時三處（Rust、ok_extract.h、外掛 .h/.cpp）一起改並先 grep 確認。
26. **`FParse::Param` 不認帶值的參數**：`-PairAudit=1,4` 用 Param 比對不到，稽核完全沒啟動 → 有帶值寫法的開關用 `Strifind`/`FParse::Value`。
27. **把女角單人 H 動作當成雙人動作**：`ReadyMotionCombo` 的 h/anim/female 狀態只動女角，男角原地站著，兩人身體重疊，
    頭髮被夾住產生數千次重置 → 雙人稽核一定要用 `PairCombo`（兩人一起播放）並選該姿勢的循環狀態。
28. **男角不發佈座標框架**：配對播放時男角 actor 不一定自己 Tick → 在 `ApplyPairedFrameWithDisplay` 裡替兩個角色都發佈，
    框架取自實際繪製骨架的元件（不是 actor）。
29. **`FParse::Value` 遇逗號就停**：`-X=1,4` 只讀到 `1` → 範圍拆成兩個參數。
30. **碰撞體依清單順序重用 PhysX 物件**：清單每幀因範圍篩選而變動，同一個 PhysX 物件從手臂跳到軀幹，
    以極大速度撞粒子（每姿勢數百～數千次重置）→ 依碰撞體身分（群組＋索引）固定對應，新出現的直接放置不掃掠，
    移動的在子步之間內插（`Advance`）；粒子剛體 `setMaxDepenetrationVelocity(1)`。修正後重置 0 次。
31. **把胸部被手臂夾住當成穿模**：ver02 胸／臀鏈半徑大，夾臂類姿勢必然被手臂與胸腔同時擠壓（無解），會淹沒頭髮／衣服的指標 →
    Rust 在身體鏈根部標 `FLAG_BODY`，稽核分開回報（`softTissue`）。
32. **PxTransform 連續賦值**：`a = b = pose` 編譯失敗（PhysX 的 operator= 回傳 void）→ 分開寫。
33. **稽核量到上一個姿勢**：換姿勢後舊姿勢的狀態清單和播放器仍在，只檢查「有播放器、有狀態」會立刻開始量（截圖每個姿勢都一樣）→
    等狀態列顯示「新姿勢 · 狀態」才開始，選定循環狀態後再等狀態列切到該狀態。數據要配合截圖抽查，不能只看數字。
34. **每條鏈一個 PhysX 場景＋碰撞體當運動學剛體**：約 100 個場景 × 多子步，幀時間 13–18 ms（Verlet 7.5 ms），
    PhysX 軟接觸造成大量跳動 → 碰撞體不進場景，改成每個子步後精確推出（非彈性）；每個動態骨骼集合一個場景、
    一次 simulate（`SolveBatch`）。結果：8.4 ms，違規 0。
35. **批次全有或全無**：集合裡一條鏈不能用 PhysX（例如只有一節），整個集合就退回 Verlet，PhysX／Verlet 逐幀交替 → 抖動。
    → 不能表達的鏈單獨走 Verlet，其餘照常批次。
36. **抖動指標**：「非預期加速度 > 3 cm/幀²」會把合理的甩動也算進去 → 另計「連續兩幀非預期加速度方向相反且都 > 1 cm」（真正的來回抖）。
37. **PhysX 模式退回 Verlet**：鏈無法用 PhysX 表達或批次失敗時改用原本的 Verlet，等於兩套物理混用（使用者明確不要）→
    `Enabled 1` 時 Rust 完全不跑 Verlet（`px::enabled()` 由外掛回報），無法表達的鏈保持不動；只有 `Enabled 0` 是原始物理。
38. **頭髮高頻抖動的真正原因（不是碰撞）**：關掉碰撞、減少投影、單一子步抖動都一樣 → 是關節本身：
    「離目標不能超過 X」用硬限制，和「固定骨長」硬約束從不同錨點互相拉扯；彈簧驅動只有對世界的阻尼、沒有相對父節點的阻尼。
    → 目標限制改成軟限制（臨界阻尼彈簧），彈簧驅動加相對阻尼（臨界）。抖動 1.84% → 0.001%，幀時間 8.4 → 6.9 ms。
    教訓：先用對照實驗（逐一關掉子系統）定位來源，再改；不要先猜。

39. **比較後端編譯**：KawaiiPhysics 的 `GravityInSimSpace`、`DeltaTimeOld` 是 private → 不能寫入，改成本地變數照 `ComputeVerletStepVelocity` 公式套用；
    區域變數名不可與 FAnimNode 成員同名（`Alpha`、`Gravity`，C4458 視為錯誤）；Chaos 幾何（`FCapsule`/`TAABB`/`Raycasts`）在 UE5.8 屬 `ChaosCore` 模組，
    Build.cs 只加 `Chaos` 會 LNK2019。
40. **測試太久**：使用者要求每項只測 10 秒、一個動作；雙人播放加速 4 倍（`-PairAuditSpeed`，PairAudit.ps1 預設 4）以在短時間內涵蓋更多週期與更大加速度。
    原始 Verlet 也要用同一套稽核量測（audit-only 旗標，只量測不改結果），否則無法比較正確性。
41. **PowerShell 5.1 讀無 BOM 的 UTF-8 腳本**：含中文（如 `後背位`）的 .ps1 用 Write 工具寫出後會解析失敗（字串缺終止符）→ 腳本一律存成 UTF-8 **帶 BOM**。
42. **Filter 含空格被截斷**：`-PairAuditFilter=插入 · 後背位` 沒加引號 → 只取到「插入」，測到正常位。PairAudit.ps1 已改成加引號；短測直接用 `後背位`。
43. **Chaos 後端整合錯誤**：(a) `FPBDSpringConstraints` 建構時用當下粒子位置算靜止長度 → 要先把粒子放到動畫姿勢；(b) 碰撞粒子群組 0 只會撞群組 0 的鏈 → 用 `INDEX_NONE`；
    (c) 粒子的 kinematic 回呼在 `MTime += Dt` 之前呼叫、碰撞粒子在之後 → 根節點要用 `LocalTime + Dt`，否則落後一個子步。
44. **使用者要求盡量用軟約束**：骨長改成距離關節彈簧（`LengthStiffness`）、碰撞改成皮膚層隱式彈簧（`ContactStiffness`/`ContactSkin`）、凍結平面改成軟限制（`PlaneStiffness`）；
    最後的精確投影只當安全網，`hard=` 統計它還動了幾次（目標 0）。
45. **PxDistanceJoint 預設 tolerance 2.5 cm**：`setTolerance` 預設 `0.025 × toleranceScale.length`，超出 min/max 這麼多才啟動；頭髮骨段只有 2–3 cm → 骨長關節（硬或軟）
    幾乎從未作用，全靠最後的精確投影（`hard=` 分類裡 length 佔大宗，剛度 2萬→40萬毫無變化就是證據）。建立時 `setTolerance(0)`。
46. **硬投影的根因是標準不一致，不是剛度**：計算動畫姿勢安全距離時，骨段中點沒扣粒子半徑，但軟接觸、硬投影有扣 → 粒子半徑大的鏈（胸部 5.4 cm）
    在自己的動畫姿勢就算「陷入」，每幀被推出、又被骨長拉回（骨長與中點修正一起高、還會抖）。四處（安全距離／軟接觸／硬投影／稽核）統一用「碰撞體＋粒子半徑」。
    教訓：調參數沒反應時，用 `OKPhysXSecondary.DebugHard N` 印出實際被修正的鏈與數值，而不是繼續掃參數。
47. **硬投影每幀上限會造成「抖動」統計**：修正被截斷後下一幀再推一次，二階差分正負翻轉 → 被算成 jitter。上限只是避免大跳，真正要做的是讓軟約束不需要硬修正。
48. **測試不要每組參數都重開遊戲**（使用者要求）：`Tools\lua\lua.exe Tools\pair_ctl.lua start` 啟動常駐遊戲 + `-PairControl` HTTP（127.0.0.1:47871），
    `Tools\lua\lua.exe Tools\pair_ctl.lua measure "<cvars>" --seconds 10` 即時改參數、重設、量 10 秒。只有改 C++ 時才需要重建／重啟。
49. **雙人動作載入慢的原因**：(a) 選一個動作會依序、單執行緒準備全部狀態（後背位 48 個）才開播 → 改成先準備預設狀態就開播，其餘 4 執行緒背景補齊，選到未完成的狀態就插隊；
    (b) 真正的大頭是「列出狀態」= 解析 Unity AnimatorController（5–9 秒），不是曲線解碼 → 加計時才看得出來（`OK pair preparation ... states=`）。
50. **磁碟快取（anim/disk_cache.rs）**：解碼後的 `CachedClip`、控制器狀態圖 `StateEntry`、資料包目錄 `ClipEntry` 用 bincode + LZ4 存檔，以資源 manifest.json 雜湊分版本。
    `Content/IndependentResources` 是連到原版專案的 junction → 快取絕不能寫進資源資料夾，放 `Saved/OkAnimCache`（UE 啟動時設 `OK_ANIM_CACHE_DIR`）。
    改了快取結構的序列化格式要把 `FORMAT` 加 1。
51. **頭髮插進身體但稽核說 0**：碰撞體依「動畫姿勢」縮小時，原本整條鏈取最差的粒子 → 仰頭時長髮的動畫目標本身就插在胸／背，碰撞體被縮到幾乎沒有；
    稽核也用縮小後的碰撞體量，所以看不到。修法：(a) 稽核加 `true=`（未縮小的碰撞體，排除鏈根所掛的身體）；
    (b) `ClearanceMode 2`：逐粒子縮小，且離鏈根超過 `ClearanceAttachMargin`（8 cm）的身體最多只讓陷入 `ClearanceMaxAllow`（1 cm）。
    margin 太小（3 cm）時，後背位裙子被夾在兩人骨盆之間會亂跳。
52. **一束長髮可能只有一根 30+ cm 的骨頭**：只檢查粒子和骨頭中點會漏掉骨頭其他位置刺穿身體 → 改成骨段與碰撞膠囊的最近點（`BoneClosest`），
    推出時依最近點位置 U 用槓桿 1/U；每幀上限要限制「接觸點」位移，而不是髮尾位移，否則靠近髮根的接觸永遠推不出來。
53. **切換動作時先用 Chaos 跑 0.5 秒**（`SettleSeconds`／`SettleBackend`，`AKKPairedPlayback::Restart` 呼叫 `OKPhysXSecondary::Settle`），對所有動作通用。
54. **PhysX 版裙子「黏在手上」被拉長**：長骨頭槓桿推出時，把粒子沿推出方向「平移」1/U 倍會把骨頭拉長，而骨長修正又被每幀 1 cm 上限卡住，拉長就一直累積
    （裙片延伸到對方手上）。改成繞固定的父節點「旋轉」（保持骨長），並在每幀上限之後，把骨長夾回容許範圍（最多伸長 10%，不受上限限制）。稽核新增 `stretch=`（最大骨長比例）。
55. **使用者可能同時在操作常駐遊戲**：量測途中動作、物理系統被切換（下拉選單），數據會被汙染 → `/stats` 第一行會列出 solver 和 pose，量完要核對。
56. **「面向鏡頭」女角轉錯方向**：(a) 用頭骨局部軸旋轉 → 上下轉變成歪頭；(b) 改用身體座標加絕對角度 → 疊在動畫已轉過的頭上就偏；(c) 用眼睛骨頭 `cf_J_Eye_tx` 判斷臉朝向、
    用胸部骨頭 `cf_d_bust00` 判斷身體前方 → 在部分姿勢判斷結果翻轉；(d) 改用鼻尖、耳根 → 這些在**臉部骨架**，身體骨架裡根本沒有（測得剩餘誤差 -1 才發現）。
    最終：臉的朝向 = 頭骨世界矩陣的 +Z（所有 Koikatsu 骨架通用），身體前方的正負號由臉決定，只補上「臉 → 目標」的最短旋轉。
    教訓：這類「方向對不對」的問題要先做**數值驗證**（`/stats` 的 `look female=剩餘/超出`），不要只靠截圖；截圖時也要注意使用者可能同時在操作。
57. **非 Verlet 的物理讓內褲撕開**：ver02（胸、臀等身體動態）在外部物理解算後（`px_done`）把整段收尾迴圈跳過，連角色卡的硬性限制
    `keep_min/keep_max` 和 `move_limit`（骨頭局部移動範圍）也一起跳過 → 臀／胯下骨頭跑出範圍，綁在兩邊的內褲網格被拉開。
    外部物理只負責「怎麼動」，角色卡的硬性限制必須照舊套用。
58. **切換物理系統後的過時狀態**：C++ 端每條鏈的上一幀碰撞體、世界座標、碰撞體縮小量的平滑值、根節點位置，在中間改跑 Verlet 時都不會更新；
    切回來時被拿來做「掃掠」與慣性 → 第一幀把布料整段拖走。超過 0.25 秒沒解算就當作全新開始（Chaos 同理）。
59. **衣服碰撞層**：頭髮／飾品（外層鏈，`FLAG_OUTER`）改撞「依實際衣服網格加厚的身體膠囊」，衣服鏈仍撞皮膚膠囊；對方角色一律以衣服表面為準。
    踩過的坑：(a) 網格 `rest_positions` 在網格自己的空間，要用「骨頭世界矩陣 × 網格自己的 bind pose」蒙皮，不是骨架的 `rest`；
    (b) 衣服部件是 `role=clothes_*` 的私有骨架網格（`skel_kind=4`、`runtime_accessory`），不是 GPU 綁定網格；用它每幀已蒙皮的 `positions`；
    (c) 鞋子只包住小腿膠囊末端，要排除；(d) 外層旗標不能套在衣服自己的鏈（裙子）上。判斷部件分類時直接把部件清單寫成檔案看，比猜快。60. **OK 動畫 FFI 的存取權**：`FOkSession::Handle` 是 private，每個要直接呼叫 ok_* 的類別（`FOkAnimBridge`、`FOkAnimPlayer`）都要加 `friend`；
    其他模組（OKAnimation）一律經過 KKUnityBundle 的橋接層，不能自己連 `ok_extract.lib`（每個 DLL 會有一份 Rust 全域狀態）。
61. **UHT：`BlueprintReadOnly` 不能和 `BlueprintSetter` 並用**（Sequencer 要呼叫的 `Set<屬性名>` setter 用 `BlueprintReadWrite`）。
62. **常駐遊戲開著 Live Coding 時 UBT 拒絕建置**（"Unable to build while Live Coding is active"），編譯檢查也一樣；C++ 變更本來就要重啟，先關遊戲再建置。
63. **bash heredoc 夾帶很長的 Rust 程式碼給 python 時解析失敗**（unexpected EOF）：改用 Write 把 patch 腳本寫到 scratchpad 再執行。
64. **把別的骨架的動畫套到 KK（或反過來）時，A-pose / T-pose 的 rest 差異不能只靠 rest 的旋轉差抵銷**：rest 世界旋轉的「扭轉」可以用
    `C d C⁻¹` 精確轉移，但骨頭「指向」不同（手臂下垂 45°）會變成姿勢誤差。解法：轉換前先把來源／目標骨架的 rest 自動對齊到家族 rest
    （`align_rest`：由上而下讓每根骨頭指向它在家族裡「延續方向最一致」的子骨頭），header 帶 `align_to` 時 `from_locals` 和 `sample_onto` 都會套用。
65. **批次輸出 .okanim 時用完整 clip key 當檔名**：KK 的 key 含很長的 controller id（十六進位），超過 Windows 路徑上限，1680 個轉換全部在存檔失敗，而且當初失敗分支沒記錯誤訊息
    → 檔名用「狀態名稱 + key 的 CRC32」，每個失敗分支都要寫出原因。
66. **OK 群組接觸的 offset 只存整段平均值**：手在大腿上滑動的動作被 IK 拉向平均點，反而改掉原動作（峰值誤差 7 cm）→ 接觸帶「逐幀 offset 軌跡」
    （`Contact.track`，均勻取樣 start..end），同體型時完全重現原動作、不同體型時依目標的 hips 高度縮放；接觸偵測也要用「到骨段表面的距離」，
    不能用關節原點距離（手放在大腿中段離兩端關節都 30 cm 以上）。
67. **驗證 OK 播放是否等於原生**：受物理動態影響的骨頭（ver02 胸部等）同一時間點連跑兩次原生結果就不同，不能算進動畫誤差；
    驗證時先跑兩次原生，自動把「自身不穩定」的骨頭分開報告（通用，不寫死骨名）。
68. **男角衣服厚度一直是 0**：雙人播放用的男角 session 第一次量衣服層時，衣服（可動配件骨架，`runtime_accessory`）還沒蒙皮，頂點都在原點，
    量到 0 個衣服頂點，結果被永久快取（簽章只看頂點陣列是否為空）。改成「有衣服部件卻量到 0」時只暫存，30 次呼叫後重量（實測 0 → 1960）。
    教訓：任何「依當下狀態量一次就快取」的值，都要能分辨「還沒就緒」和「真的是 0」。
69. **`-ExecCmds` 的多個指令用逗號分隔**，不是分號（分號會被當成同一個指令的參數）。
70. **同一個專案可能有其他 session 同時在做**（例如「物理引擎比較：Bullet 和 Jolt」）：建置前「關掉所有 UnrealEditor」會把別人的遊戲一起關掉，
    別人的遊戲（或它的崩潰對話框）也會讓 Live Coding 鎖住建置。規則：只關自己啟動、記下 PID 的程序；要動別人的程序先用 SendMessage 協調；
    遇到不認得的控制端口指令（如 `Backend 4`）先查 ListAgents，不要當成自己的問題去改。
71. **`FQuat::AngularDistance` 不能拿來量小誤差**：它用 `acos(2·dot²−1)`，float 精度的四元數 dot 只要比 1 小 1e-7，acos 就放大成約 0.08°。
    自我測試因此出現「同一份資料重算兩次差 0.079°」的假誤差，追了好幾輪才發現是量測工具的問題。改用 `2·atan2(|v|,|w|)`（`FOKUnrealSkeleton::AngleDegrees`），真實誤差全部是 0。
    教訓：量到「固定大小、跟操作無關」的誤差時，先懷疑量測方法本身。
72. **讀 Unreal AnimSequence 時要用原始資料**：`GetBoneTransform(..., bUseRawData=false)` 會讀壓縮資料（專案啟用 ACL，有損），「展開成 AnimSequence → 寫回」因此有 0.08° 誤差；
    編輯器內強制讀原始 key（`true`；沒有原始資料時自動退回壓縮資料）後往返誤差為 0。
73. **自動化擷圖只能擷自己程序的視窗**（PrintWindow + DPI-aware）：用桌面擷取時前景切換可能失敗，會擷到使用者正在用的畫面；擷到就立刻刪除。
74. **重建既有 SkeletalMesh 時不要自己清空 `GetLODInfoArray()` / `LODModels`**：在外掛 DLL 內清空引擎持有的 LOD 資料會在 `mi_free` 存取違規（commandlet 當掉）。
    重用既有資產時保留 LOD 結構，只用 `CreateMeshDescription` + `CommitMeshDescription` + `Build` 替換幾何。
75. **SkeletalMesh 的 MeshDescription 需要 `StaticMeshDescription` 模組**（`MeshAttribute::VertexInstance::*` 名稱定義在那裡），只加 `SkeletalMeshDescription` 會連結失敗。
76. **由骨架 rest 產生的棒人預覽**：根骨到其子骨（地面 → hips）的方柱不能畫——它綁在不動的根骨上，hips 一動就和身體脫節。
77. **控制埠 `/exec?cmd=` 不解碼 `+`**：`curl --data-urlencode` 把空白編成 `+`，指令會變成 `OKAnim.Fade+A_WLoop+0.3`（回 `??`）。
    空白要編成 `%20`（用 `urllib.parse.quote`；scratchpad `c.sh` 的 `c` 函式）。
78. **物理動態骨（ver02 胸部等）不能用「連跑兩次原生」判斷**：同一時間點原生→原生結果相同，但結果取決於前一個姿勢，OK→原生就不同，
    驗證誤差每次跑都不一樣（bust02 0.8～5°）。改成直接問角色哪些骨頭由動態模擬（`ok_session_dynamic_bones`，來自 BodyDynamicSet），分開報告。
79. **淡入量測要只看動畫驅動的骨頭**：角色自己的附加骨架（`cf_J_Vagina_Pivot_*` 等）不在 OK 動畫內、由其他系統驅動，量「相鄰幀最大變化」會被它們的 160° 跳動蓋過。
    淡入的合理驗收：每幀變化 ≤ 瞬切跳變 / 淡入幀數 + 動畫本身每幀變化（不是「≤ 單幀動作變化 × 2」，兩個姿勢差很多時不可能）。
80. **Montage 區段有淡入重疊時，分支不能用「目前在哪段」判斷**：下一段在本段結束前就開始淡入，越過下一段起點時本段已不算「正在播放」，分支永遠不觸發。
    改成記錄上一個 tick 的時間，判斷「這兩個 tick 之間是否跨過本段邊界」。
81. **改了轉換格式（加曲線等）後，批次輸出要整批重跑**：批次會跳過已存在的檔案，舊檔不會補上新資料；格式變動就刪掉輸出資料夾再跑。
    批次在遊戲中每幀 40 ms 只有 ~2.4 個/秒（`ok.Anim.BatchBudgetMs` 後來由 #82 的背景批次取代）。
82. **大量讀 KK clip 要在「動畫預備範圍」內**（`begin/end_animation_preparation`）：範圍外每載入一個 clip 都重新解壓、解析整個 bundle，
    單一 clip 200～480 ms（8959 個 clip 的 bundle 更慢），開 6 執行緒也只有 2.7 個/秒且吃滿 8 核。範圍內同一 bundle 只解析一次，載入降到 20～30 ms。
    批次改成 Rust 背景執行緒（`ok_okanim_batch_*`），以「來源」為單位分配給執行緒，每個來源結束就結束範圍釋放記憶體。
83. **「先問大小再複製」的 FFI 輸出不能每次重算會變動的內容**（例如背景進度）：兩次呼叫之間內容變長，第二次 cap 不夠就回 -1，
    C++ 端看起來是「poll 失敗」。查詢大小（out = null）時存快照，複製時回傳同一份快照。
84. **測試用遊戲視窗會擋到使用者**：遊戲視窗在測試中被關閉（log：`RequestExit ... ViewportClosed`），很可能是擋到使用者被手動關掉。
    最小化啟動不行（遊戲一直 `ready=0`，不會開始播放）。改用 `RunControl.ps1 -Background`：最小化啟動，視窗出現後
    `ShowWindow(SW_SHOWNOACTIVATE)` + `SetWindowPos(HWND_BOTTOM)`，不搶焦點、放在所有視窗後面，遊戲照常就緒。
85. **commandlet 測試不能在固定路徑重建資產再存檔**：第二次執行時該套件已存在、只被部分載入，`CreatePackage` 拿到舊套件、
    新物件存檔就 `appError: cannot be saved as it has only been partially loaded` 當掉（OK_AnimBP、KK_FamilyEdit 各中一次）。
    只為驗證的物件放 `GetTransientPackage()` 不存檔；必須驗證存檔時用每次唯一的套件名，驗證後刪檔。
87. **接觸 IK 的四個連鎖問題（侍奉姿勢，男高 1.0 / 女 0.0 時腳趾→膝蓋誤差 14 cm）**，`OKAnim.Matrix` 每格印出最差接觸才找到：
    (a) 效應骨在肢體末端之下（`cf_j_toes`、手指）完全沒做 IK → 找「承載的肢體」；(b) KK 的 `cf_j_foot` 與腳踝同位置，以它為末端的雙骨鏈長度 0，
    且腳趾、腳、腳踝會變成三條重疊的鏈 → 取最高的可用末端（腳踝）；(c) 同一肢體還有 MotionIK「自身根骨」連結，後解的覆蓋先解的
    → 每肢體只解一個：伙伴接觸優先於自身連結；地面鎖腳也讓位；(d) 目標骨（男方膝蓋）在第一輪後面才被他自己的 IK 移動 → IK 跑兩輪；
    另外 IK 後保持末端世界方向，腳趾偏移才固定。修完 16 組全部 0.0 cm。接觸誤差統計只算伙伴接觸（自身連結為了碰伙伴而偏離是預期的）。
88. **配位不能每幀直接套用**：循環內配位 offset 每幀微幅擺動，用「offset 速度」判斷走路會在正常雙人播放時混入走路（權重 0.88）。
    改成配位以步行速度（每秒 1.2 個 hips 高）趨近目標，走路權重看「還沒走到的距離」；新群組從原地走入，dt = 0 或時間跳轉才瞬移。
89. **OK 解算的每幀成本（R6.1）**：姿勢策略的貪婪試探每次都呼叫 `ids_by_name`（每次重建整個骨名 HashMap）與沿父鏈的 FK，
    雙人時 GT 由 12 → 25 ms、偶發 100 ms 尖峰。改為每次 apply 內快取骨名表、rest 世界旋轉跨幀快取、姿勢策略只在上一幀伙伴接觸
    未滿足（> 0.015 hips 高）時執行、一輪無改善即停。量測用 `OKAnim.Profile`（Rust 分段累計＋雙人 Advance 總時間）與
    `stat dumpframe -ms=0.25 -root=FrameTime`（看出 KKPairedPlayback Tick 佔 13 ms）。
90. **平行化兩個角色的收尾流程沒有用**：配件動態等內部已用執行緒池平行，兩角色同時跑只是搶同一個池。有效的是跨幀重疊：
    `ok.Anim.AsyncPair`（預設 1）把下一幀的 OK 解算丟到背景工作、下一個 tick 開頭等它並顯示（1 幀延遲），
    雙人 GT 10.2 → 7.9 ms（後背位 122、侍奉 126、正常位 120 FPS）。任何從外部讀角色 session 的程式先呼叫 `AKKPairedPlayback::SyncAsync()`
    （`ActivePair()`、Stop、Restart 已處理）。
86. **標頭的 namespace 內不能用 `class X*` 當參數型別順便宣告**：`namespace OKAnimationEditing { void F(class UOKAnimMontage*); }`
    會宣告出 `OKAnimationEditing::UOKAnimMontage`，實作端拿真正的類別就「使用未定義類型」。前置宣告放在 namespace 外。
91. **骨頭附加順序依 HashMap 迭代順序**：`Skeleton::append_missing_tree_bones`／`merge_missing_tree_transforms` 從 `tfs`（HashMap）
    收集要附加的骨頭，同一張卡每次開啟的 bone 索引順序不同（例：`k_f_dan_entry` 與 `cm_J_dan102_00` 互換），session 位元組不可比對。
    改為依 transform id 排序、同名取最小 id。任何會進入輸出的順序都不能來自 HashMap 迭代。
92. **OK 基礎庫不能是「搬 Unity 資料」**：第一版用假遊戲根目錄（bundle 換代號檔、zipmod 副本、清單 bundle 原樣）＋查詢結果，
    使用者要的是把 Unity 資源轉成 OK 模型（`.okmodel`＋`okcatalog`），庫內不能有任何 Unity 檔案。已移除假根目錄；
    查詢錄製層保留當轉換器用。設計前先確認「交付物的格式」而不只是「能不能跑」。
93. **OK 基礎庫不能靠「開卡錄製」來涵蓋資源**：用合成角色卡遍歷清單項目，一張卡 136–229 秒、需 4130 張，且使用者要的是「資源轉換」不是「卡片轉換」。
    改為 `--oklib convert` 直接逐個 Unity 來源列舉所有內容（prefab、貼圖、材質、著色器、骨架、動態骨）轉成 `.okmodel`，21,861 個來源約 2 小時。
94. **查詢結果不可依賴呼叫順序**：`exact_texture_location` 在貼圖目錄建好前後走不同規則（目錄建好後忽略容器別名）、
    `named_png_cached_fallback` 依目錄是否「完整」決定要不要整包重讀。錄下來的模型無法重現這種內部狀態。
    改成單一規則（容器別名優先、否則第一個同名物件），模糊查找改由可序列化的貼圖目錄計算；live 輸出經驗證不變。
95. **模型體積**：網格紀錄內嵌的 PNG、`decode_all`／`base_bundle` 重複網格、每個網格的骨架都含完整 transform 表。
    以內容定義分塊（平均 16 KB）在模型內去重、貼圖改存原始 texel（PNG 由同一解碼函式產生）、網格貼圖改存「欄位→貼圖→轉換」引用
    （寫入前驗證推導結果與原位元組完全相同才替換），31 個身體來源 418 → 212 MB（約來源的 1.1 倍）。
96. **轉換中的 exe 被鎖住**：背景轉換使用 `target/release/ok-extract.exe` 時 `cargo build` 會失敗（無法刪除 exe）。開發測試改用 `--target-dir target_dev`。
97. **bash heredoc 裡的 Python 字串跳脫**：在 `python - <<'EOF'` 內寫 `b"\x89PNG\r\n"`，寫進 Rust 原始碼時變成真正的控制字元
    （「byte string literal 含非 ASCII」）。含反斜線跳脫的修補腳本一律用 Write 工具寫成 .py 再執行。
98. **中斷轉換會遺失來源索引的最後一段**：`sources.okidx` 每 25 個來源存一次，強制停止行程後，已寫好的模型在索引裡找不到，
    重播時被當成缺少。`convert @清單` 改為不略過已完成的來源（已有的紀錄自動跳過），用來補索引與新增的列舉查詢。
99. **服裝卡外掛資料是「服裝層級」格式**：Sideloader 屬性沒有 `outfitN.`、ME/DynamicBoneEditor 用 `CoordinateIndex` 0、ASS 用 `Coordinate` -1、
    KCOX/Pushup/髮飾/Accessory_States 是單一值而角色卡是 `{套: 值}`、moreAccessories 沒有 `accessorySet`。
    穿上／匯出要逐外掛轉換；以「角色卡匯出→穿回同一套→session 相同」驗證。
100. **多 prefab 的 bundle 轉換呈平方級成本**：`meshes_for_maindata` 每次呼叫都重掃整包的材質／著色器／貼圖、重建 transform 圖
     （`ensure_move_nodes`、`ensure_prefab_locals` 各自 `build_skel_graph`）、衣服用的 `clothes_renderer_map` 每次重載整包。
     原版後髮包（178 prefab）一個來源 507 秒。改為材質對應表、狀態節點鏈、渲染器類型、transform 圖以 bundle 為單位快取，
     衣服專用查詢只對衣服類來源轉換，轉換時「存精簡版、回傳原值」（`memo_packed`）：155 秒；live digest 不變。
     量測用 `OKLIB_PROFILE=1`（各查詢與內部階段的累計秒數）。
101. **bincode 資料不能用 `skip_serializing_if`**：`ExtractedMesh.texture_plan` 標了「空就不序列化」，bincode 不自描述，讀回時錯位，
     42,819 筆網格紀錄中 2,481 筆無法解碼（重播時表現為查詢缺口）。移除該屬性，`repack` 以「先嚴格解碼、失敗才用舊格式探測」
     遷移（探測用有上限的 bincode 選項，避免讀到巨大長度時配置記憶體直接中止行程），`check` 驗證全部可解碼。
     任何進 bincode 的結構只能加欄位、不能條件省略欄位。
102. **查詢鍵大小寫**：貼圖查找不分大小寫，但記錄時用目錄的小寫名、開卡時用清單的原始大小寫 → 鍵不同。不分大小寫的查詢一律以小寫為鍵。
103. **在共用結構加欄位會破壞所有既有的 bincode 資料**：`ExtractedMesh` 也會序列化進 session 預覽（飾品來源網格）、okPv 可攜卡、
     快取與 v1 資源包；今天新增的 `texture_plan` 讓這些舊資料的版面錯位（live digest 跟著改變就是警訊）。改為只在 OK 基礎庫紀錄
     （`with_texture_plans` 執行緒旗標）內序列化，其餘場合版面與原本完全相同，live digest 回到 20ad6607／9ba849ca。
     新欄位要嘛放在新的外層結構，要嘛用旗標限定在新格式內，絕不直接改動已持久化的結構。
104. **`ChaListCatalog::get_slot` 的後備依 HashMap 迭代順序**：類別找不到時在「任意類別」找 id 相符的項目，用 `values().find`，
     多個候選時每次執行結果不同（同一張卡的短褲欄 live 是「おばけそで左用」、另一次是「宝石4」）。改為固定規則
     （slot id 優先、再取最小類別號）。基礎庫驗收靠 digest 比對才抓得到這類既有的不確定性。
105. **prefab 名稱不能改用小寫重試**：部分 prefab 查找區分大小寫，用小寫名稱查會掉進模糊網格比對、抓到別的物件。
     名稱一律照原樣作為查詢鍵，靠列舉（容器根、場景根、清單提示＝以 bundle 檔名配對）涵蓋。
106. **遊戲能載入的卡，流程不能直接報錯**：卡片 KKEx 指定的 mod 頭沒安裝（清單無此項目）→ 遊戲用預設頭；飾品欄存了非飾品類別 →
     遊戲不產生飾品。原本流程對兩者都回傳錯誤、整張卡開不起來。改為記警告並照遊戲行為處理（6 張原本失敗的卡現在可開）。
107. **驗收方式**：一次跑 498 張（約 1.5 小時）時，中途發現的問題會讓整輪作廢。改為每批 10 張，前 100 張全過再擴大；
     驗收前先確認一般模式本身是確定的（同卡兩次 digest 相同），否則差異可能來自既有的不確定性而非基礎庫。
108. **UE5.8 的 `FJsonObject::Values` 鍵是 `UE::TSharedString`，不是 FString**：`for (auto& C : Obj->Values) Map.Add(C.Key, ...)`、
     `C.Key.Split(...)` 都編不過。一律 `FString(*C.Key)` 轉換。另外 OK_Demo 把 C4458（區域變數遮蔽成員）當錯誤，
     區域變數不要與 UPROPERTY／Toolkit 成員（如 `Materials`、`Toolbar`）同名。
109. **`UCameraComponent::SetCameraMesh(nullptr)` 讓編輯器當機**（讀取位址 0x18）：想隱藏預覽 actor 上 StudioLens 的相機圖示。
     改為 `DestroyComponent()` 移除預覽用不到的相機元件。另：自測等待條件要有獨立的計數器，否則「等不到就照跑」會在錯誤狀態下執行並當機。
110. **預覽不能自己再寫一套「網格 → SessionPart」**：模型編輯器預覽先用 `blend_for_mesh("model")`＋`from_mesh` 自建 part，
     結果 (a) role `"model"` 讓 UE 選錯著色路徑；(b) 原版衣服的 MainTex／ColorMask 來自清單（`CreateClothesTexture`），
     prefab 材質裡是空的 → UE 預設棋盤格；(c) 衣服遮罩未依卡片流程處理。改為：role 由重播的清單查類別、
     清單貼圖以預設色烘焙、最後交給 `push_meshes`（卡片流程同一個收尾）。預覽＝「卡片流程單獨穿這件」。
     比對工具 `--oklib model-diff <lib> <id> <prefab> <card>` 直接列出預覽 part 與卡片同名 part 的設定差異。
111. **蒙皮網格沒有骨架就不能直接畫**：基礎庫的 `ExtractedMesh` 頂點是網格空間，預覽 session 沒有身體骨架時蒙皮結果散成碎片。
     預覽時以 prefab 自己的 `smr_skel`（`set_bone_order(bone_names)`）蒙皮成綁定姿勢，再清掉權重轉成靜態（`skel_kind 0`）。
     對焦也不能用 `GetComponentsBoundingBox`（程序網格元件有固定的大範圍 bounds、還有相機元件），改用 session 頂點（Unity→UE：(-x, z, y)×100）。
112. **MSVC 參數求值順序**：`Check(ReadColor(After) && ..., TEXT("...") + After.ToString())` 中字串先被組好，印出未初始化的值；
     自測的結果與訊息必須先算成區域變數再傳入。
113. **讀原作卡的 zipmod 後備不能開放整個遊戲 `mods/`**：第一版把遊戲 `mods/`（約 2 萬個 zipmod）合併進目錄列表，
     mod 清單載入於是逐一打開每個實體 zip，一張卡跑 20 分鐘以上，也違反「只用此卡的 zipmod」。改為依卡片的
     Sideloader 解析清單（`resolves`）與 uncensor GUID，以 `mods::find_guids`（有索引快取）找出此卡需要的 zip，
     只把這些以 `mods/<檔名>` 開放；卡片目錄的 zip 同樣處理；基礎庫已記錄的 zip 不重複列出。後備只回應 `mods/`，
     原版資料一律由基礎庫提供。
114. **錄製卡片資源包時，覆寫層必須「只寫不讀」**：原作卡轉 APNG 卡時，後備讀到的 zip 項目被記成 `Entry::Model(content id)`；
     若同一次開卡就從覆寫層回答查找，第二次讀同一項目拿到的是模型代號而非實體 bytes，凡是第一次沒經過 memo 的查詢
     （ParsedBundle 內部快取直接回答的）就補不回來 → 身體遮罩消失、甚至少了一半部件。改為：錄製中覆寫層只寫入，
     查找只在重播 APNG 卡時使用；開始錄製前清 bundle 快取，確保每個查詢都經過 memo 而被錄下。
     除錯工具：`--oklib pack-diff <lib> <卡>`（同一張卡「不錄」與「錄」兩次開卡必須逐位元組相同）。
115. **基礎庫目錄內的 `work/`、`models/` 不是遊戲資料**：驗收把 APNG 卡寫在 `<lib>/work/okcards/`，`gamefs` 把它當成
     基礎庫內容去索引找而讀不到。`logical()` 排除這兩個目錄。
116. **「呼叫者沒給遊戲目錄」不等於「沒有遊戲目錄」，「session 是否可攜」也不能由參數推斷**：UE 獨立卡模式開卡傳空字串、
     設定的 `KKGameRoot` 在這台機器又不存在 → 卡片 zipmod 找不到（`named zipmods 0`）。改為：給的目錄沒有 `mods/` 就用
     `OK_GAME_ROOT`（UE 由 `KKGameRoot` 自動填）。另外 UE `bPortable = GameRoot.IsEmpty()` 讓基礎庫 session 被當成遊戲模式，
     動作改從遊戲目錄載入而失敗（`OK source walk could not load`、配對 ready=0）；改由 Rust 回報 session 是否有遊戲根目錄。
117. **開卡快取的 key 要涵蓋所有輸入**：KKS 卡第一次在沒有 zipmod 的情況下開啟，結果被寫入快取；之後 zipmod 可用時仍命中
     那份舊結果。key 加入後備可提供的 zipmod（路徑、大小、修改時間）；log 也標示「cached session」以免誤判。

## 移植到 OpenKoikatsu 後（2026-10-15）

### 暫時 session 的身體成了幽靈對手
- 現象：OK_Demo 的雙人稽核裡，很多姿勢出現 `byBody=p2:…`、`p7:…`，也就是和「對方身體」的大量違規。移植後把 PhysX 限定在互動中的兩個角色，這些違規消失：基準平均 1,752 次，移植後 380 次。
- 原因：
  - 背景開的暫時 session（換衣服預熱、外觀候選、讀卡工作）也會呼叫一次 `publish_body`。
  - 這些群組從不移除，也沒有世界座標框架（`SetGroupFrame` 只有角色才會呼叫）。
  - 它們的身體膠囊因此留在原點，被當成雙人場景的對方身體，頭髮和衣服一直和它碰撞。
- 修正：
  - PhysX 只給登記過的角色（`AKKPreviewActor::AddPairPhysics` 加上 Rust 的 `ok_session_set_physx_secondary`），暫時 session 不再送出身體。
  - 跨角色碰撞時，略過座標框架狀態和自己不同的群組。
- 預防：
  - 送進 PhysX 的東西都要有擁有者與生命週期。沒有擁有者的資料不能進共享的全域表。
  - 稽核數字突然「變好」時，先找原因再採信。

### move assignment 把 PhysX 開關蓋掉
- 現象：雙人場景中，對方角色換卡、換衣服後，就回到內建 Verlet。
- 原因：讀卡工作先在暫存的 `FOkSession` 開好，再 `Actor->Session=MoveTemp(Job->Session)`。move assignment 把暫存 session 的開關（關）也一起複製過去了。
- 修正：開關屬於角色，所以 move assignment 時保留目標原本的值，並套用到新的 native session。

### 合併取了 OK_Demo 的模組宣告，遊戲版連結失敗
- 現象：StreetSocial 遊戲版（monolithic）連結時出現大量 `GInternalProjectName` 無法解析；Editor 版正常。
- 原因：三方合併時 `OpenKoikatsu.cpp` 直接取了 OK_Demo 版。OK_Demo 的主模組是 `OK_Demo`，所以那邊的 OpenKoikatsu 用的是 `IMPLEMENT_GAME_MODULE`；本專案的 OpenKoikatsu 必須是 `IMPLEMENT_PRIMARY_GAME_MODULE`。
- 預防：從另一個專案合併程式碼時，「只有對方改過」不等於可以直接取用。專案層級的宣告（主模組、target、uproject）要逐一比對。

### 角色框架取到剛體配件，臀部抽動被當成整個人在動（2026-10-16）
- 現象：一對一互動（C050，白褲襪＋灰短褲，後背位）中，PhysX 開著時短褲後片被甩開，露出褲襪在短褲底下被裁掉的區域，看起來像屁股、陰部破圖。內建 Verlet 同一姿勢不會。關掉自身碰撞或跨角色碰撞都不會消失，所以不是碰撞。
- 原因：`AKKPreviewActor::PublishPhysicsFrame` 用 `Parts` 第一個元件的世界變換當角色框架。C050 的第一個元件是跟骨頭走的剛體配件（`RigidFollowBone`，每幀 `SetRelativeTransform`）。框架因此每幀晃 1–3 cm、轉約 1°。WorldInertia 把它當成角色在世界中移動，每條鏈都多了假的慣性；跨角色碰撞換算對方身體也用這個框架。
- 量測（後背位，速度 4，8 秒）：pops 564→0、jitter 580→5、maxJerk 0.234→0.021、穿透違規 49→0、hard 修正 42003→3532。修正後 12 幀取樣都不再露出，和 Verlet 一致。
- 修正：框架只取畫骨架的元件，略過 `RigidFollowBone`、執行期配件和掛在插槽上的元件；都沒有時用 `BodySkel`，再沒有用 actor。
- 預防：
  - 「角色在哪裡」要取不隨動畫動的東西。拿「第一個」元件之前，先確認陣列裡每種元件的變換是誰在寫。
  - 衣服在 PhysX 下亂跑時，先分開開關：Enabled、SelfCollision、CrossCollision、WorldInertia。只有 WorldInertia 能消掉的，就去查框架。
- 未解決（不是 PhysX）：同一姿勢陰部輪廓凸出褲襪，Verlet 也一樣。是身體生殖器網格與褲襪的問題，需另外處理。

### 替身身體的皮膚鏈被身體膠囊推開（2026-10-16）
- 現象（推論，C050 沒有這種鏈，未在畫面上重現）：BP 等替身身體把普通 DynamicBone 放在陰部／肛門 Pivot 骨上。PhysX 讓它們和自己及對方的身體膠囊碰撞，皮膚被推離動畫姿勢，但褲襪沒有綁這些骨頭，皮膚就穿出來。
- 原因：遊戲與 Rust 內建路徑都不給這類鏈身體碰撞體（`configure_shared_clothes_colliders` 的 body component 分支，見 CHARACTER_EDITOR_ERRORS E131），PhysX 卻把身體膠囊加回去。
- 修正：Rust 在根粒子標 `FLAG_BODY | FLAG_SKIN`（16）。主機看到 16 就不加身體膠囊，也不套 Weight／Hardness。`OKPhysXSecondary.SkinBodyCollision 1` 可恢復舊行為做比較。
- 診斷：環境變數 `OK_PX_CHAINS=<檔案>` 會在衣服動態建好時列出每條鏈（元件、是否替身身體、骨頭）。

### FLAG_SKIN 誤標頭髮，主機解算器的頭髮不撞身體、也不吃重量／柔軟度（2026-10-17）
- 現象：比較各解算器的重量與柔軟度時，PhysX、Kawaii、Jolt、MuJoCo 對重量 10 幾乎不下垂（drop < 5 mm），只有內建 Verlet 有反應；PhysX 的 maxPenetration 一直是 0。
- 原因：前一天「替身身體的皮膚鏈」修正用「沒有衣服元件名稱、沒有祖先、沒有 overlay」判斷皮膚鏈。頭髮與飾品的動態集合也符合這個條件，根粒子旗標成了 27（含 BODY 與 SKIN）。主機因此跳過身體膠囊，也不套重量和柔軟度。
- 修正：只有 `update_clothes_dynamics` 建的衣服集合（`mark_skin_chains`）才可能有皮膚鏈。
- 預防：
  - 新旗標要在主機端印出實際收到的旗標分布再收工，不能只看想修的那條鏈。
  - 驗收截圖只看到想看的部位（短褲）；頭髮少了身體碰撞沒有被任何指標抓到。之後改動旗標要跑一次全解算器的 pen／drop 表。
- 量測（後背位，速度 1，6 秒；drop＝動畫姿勢比模擬高多少，公尺）：重量 1→10 時 Verlet 0.003→0.095、PhysX 0.023→0.141、Kawaii 0.003→0.092、Chaos 0.105→0.171、Jolt 0.005→0.102、MuJoCo 0.003→0.102。

### Kawaii 的重力隨子步消失（2026-10-17）
- 現象：Kawaii 對重量設定沒有反應。
- 原因：Kawaii 把重力當速度、每個子步加 g×Sub。配上它的位置拉回，平衡下垂量是 g×Sub／pull，子步越小越接近 0。
- 修正：改成和 DynamicBone 一樣，每個參考步加 g×RefStep² 的位移，平均分到子步。柔軟度也套到 Kawaii 與 Chaos。Kawaii 沒有「偏離範圍」限制，所以柔軟度只影響拉回與阻尼。

### 龍尾巴穿透男主身體（2026-10-17）
- 現象：C050 的龍尾巴（飾品 `a_n_waist_b`，`tail_M_02`～`end` 共 11 粒子）在後背位直直往上插進男主胸腹。Verlet 和 PhysX 看起來一樣；稽核卻說沒有穿透（maxPenetration 0.007）。
- 原因（三個疊在一起）：
  1. 作者半徑全是 0：碰撞只看骨頭中心線，網格卻粗約 7 cm（根部）→ 1 cm（尖端）。稽核用的也是 0 半徑，所以看不出來。
  2. 前兩節彈性 1、剛性 1，等於釘在動畫姿勢上。精確修正每幀最多推 1 cm（HardMaxStep，防跳動），下一幀模擬又把它拉回去，修正量永遠累積不起來。
  3. ClearanceMode 2 把「鏈根 8 cm 內的身體」當成鏈掛著的身體，只阻止超出動畫姿勢的部分。後背位時男主骨盆就在她腰後，男主也被當成掛著的身體。
- 修正：
  1. Rust 從飾品網格算每段骨頭的粗細（主要綁在該骨頭上的頂點到骨頭線段的中位數距離，換成世界單位，上限 15 cm），放進新欄位 `OkPxParticle::body_radius`。主機只在碰撞**對方身體**時用 max(作者半徑, 網格粗細)；自己的身體照舊（頭髮不浮起來）。`OKPhysXSecondary.MeshRadius 0` 可關。
  2. 精確修正的推移量跨幀保留，每幀乘 `OKPhysXSecondary.PushMemory`（0.9）。接觸持續時幾幀內推到底，仍然沒有單幀跳動；接觸消失後自然回彈。
  3. 「掛著的身體」只算自己的身體，對方身體一律要避開（`OKPhysXSecondary.PartnerAttach 1` 恢復舊行為）。
- 量測（後背位，速度 1，8 秒）：PhysX 對方身體違規 62（p16:72）→ 0，pops 19→18、jitter 1→0。正側面截圖中，尾巴改從男主身側彎出。其他解算器開關 MeshRadius 時 pops／jitter 差不多（那是它們本身的特性）；網格半徑讓 Chaos、Jolt、MuJoCo 違規減半，Kawaii 對男主軀幹略增（0.051→0.084）。推移記憶只在 PhysX（或 AltSafetyNet 1）。
- 預防：
  - 碰撞半徑為 0 的鏈，稽核量不到網格穿透。看畫面判斷穿透時，先印出鏈的半徑（`OK_PX_CHAINS` 現在也列飾品集合與 `mesh=` 粗細）。
  - 「每幀上限」配上「每幀從模擬重來」等於永久上限。限制速度的地方要確認修正量能跨幀累積。

### 雙人場景的相機離人物太遠（2026-10-17）
- 現象：一對一場景選好姿勢後，人物只佔畫面下方一小塊。相機距離 4.8 m，曾出現 25 m；取景演算法卻判定垂直方向已撐滿（ndc_y 0.92）。
- 原因：選姿勢時 `FramePairedPose` 用即時取景（`OkFitCaptureRig` Realtime），取程序網格區段的 `SectionLocalBox` 和骨架網格的 `CalcBounds`。GPU 蒙皮時頂點在 GPU 上移動，這些盒子停在參考姿勢（站立、張手），所以相機框住的是兩個站著的人；某個區段的舊盒子更大時，就會出現 25 m。
- 修正：即時取景改用目前姿勢的主要關節（頭、頸、胸、骨盆、手臂、手、大腿、膝、腳踝、腳趾），各自外擴一個身體半徑（`AKKPreviewActor::AppendJointFramePoints`）。找不到足夠的關節時退回原本的盒子。`PublishPhysicsFrame` 與取景共用 `SkeletonWorldTransform()`。
- 量測：後背位取景距離 4.8 m → 2.5–3.0 m，兩人約佔畫面高度 80%、置中。
- 預防：GPU 蒙皮的元件，網格包圍盒不代表目前姿勢。要知道人物在哪，用骨架。

### 雙人動作切換要等 8–11 秒（2026-10-17）
- 現象：選一個沒選過的雙人姿勢，要 8–11 秒才開始播；取消後再選別的也一樣慢。
- 原因：
  1. 選姿勢時要先列出雙方動畫控制器的所有階段（`AnimationSourceNames` → `controller::entries`），冷解析一個控制器約 4–5 秒，一個姿勢兩個。解析結果有磁碟快取（`Saved/OkAnimCache`），但這份工作複本是新的，99 個姿勢幾乎都沒快取。程式本來就是第一個階段準備好就播，慢在這之前。
  2. 第一個階段的動畫片段冷解碼要 2.7–4.4 秒。
  3. 前一個姿勢在背景準備其他階段時一直握著準備鎖，新的選擇要多等約 1 秒。
- 修正：
  1. 姿勢清單一建好，就在一條低優先權執行緒預熱全部控制器，以及每個姿勢預設第一個階段（WLoop／Idle）的雙方片段，寫進磁碟快取。每次啟動只跑一次。
  2. 第一個階段交出去後就放開準備鎖。
- 量測：預熱第一次要 994 秒（控制器 198 個）加 203 秒（片段 198 組），之後每次啟動只要 1.8 秒（從磁碟讀）。預熱後選 7 個沒選過的姿勢，從點選到開始播放 30–60 ms（原本 8–11 秒）。
- 預防：新的工作複本或清掉 `Saved/OkAnimCache` 後，第一次啟動的前 20 分鐘，沒預熱到的姿勢仍會慢。
- 另：同一天出現一次渲染執行緒崩潰（`FReadOnlyMeshDrawSingleShaderBindings::SetShaderBindings`，讀 0x800000004），發生在雙人姿勢清單建立前（預熱還沒開始），之後一次 ZenServer 連線卡住，重開就好。未重現，原因未查。

### PhysX 讓瀏海掀開、髮束打架（2026-10-17）
- 現象：C050 在 PhysX 下，瀏海往上、往旁邊掀開露出額頭，髮束互相交叉；內建 Verlet（原作）瀏海自然垂下。Kawaii、Jolt、MuJoCo 接近 Verlet，Chaos 更亂。
- 逐項排除：關自身碰撞、關對方碰撞、關世界慣性、硬限制、凍結平面、骨長彈簧、阻尼，單獨關都沒用；「接觸軟層＋精確修正」一起關才好。
- 原因：
  1. PhysX 的接觸軟層和精確修正除了粒子，還檢查骨段中點。瀏海沿著頭部的球形 hit collider（卡片的 DynamicBone 碰撞體）排列，每段骨頭都是穿過球的弦，兩端粒子在球面上時中段一定在球內，於是一直被往外推，加上 6mm 軟層與預判距離。拉回力很弱（彈性約 0.04）的瀏海就被推翻。DynamicBone 只檢查粒子。
  2. 同一個頭髮飾品有 3 個網格部件，各自模擬一份同樣的瀏海鏈。當天加的「網格粗細」按各部件自己的網格算（0.013／0.016／0），碰到對方身體時 3 份模擬各走各的，髮束交叉。
- 修正：
  1. 鏈自己的 DynamicBone 碰撞體（`AuthoredShapes`）照 DynamicBone：只碰粒子球，不加軟層、不檢查骨段。身體代理膠囊照舊。
  2. 同一掛點、同名骨頭的網格粗細在所有部件間取最大值（`AttachedDynamicSet::apply_mesh_radii`，每幀在 `update_dynamic_accessories_impl` 開頭統一）。
  3. 頭髮／飾品不和離根骨最近、8 cm 內的自身代理膠囊碰撞（`OKPhysXSecondary.SkipRootBody`，預設 1），交給卡片自己的碰撞體。
- 量測（玩弄龜頭，頭後仰 50° 稽核）：穿透違規 432 → 0，pops 2577–3054 → 0，jitter 約 1000 → 0。正面近拍瀏海垂下蓋住額頭，中間仍比 Verlet 多一道分線，那時已與碰撞無關（關掉所有碰撞仍在），是 PhysX 彈簧動態的差異。
- 預防：
  - 碰撞先問「原作怎麼碰」。DynamicBone 碰撞體只作用在粒子；骨段檢查只適合我們自己的粗略身體代理。
  - 同一飾品多個網格部件會各自模擬；任何按部件算的參數都要在部件間一致，否則同一束頭髮會分家。
  - `BodyMask` 會同時遮掉對方的同號膠囊，單獨用它判斷是哪個膠囊會誤判。

### 瀏海分線的通解：原作 DynamicBone 更新＋只做投影的碰撞（2026-10-18）
- 現象：上一條修完碰撞後，PhysX 瀏海中間仍多一道分線、髮束偏開；關掉所有碰撞仍在。
- 試過：放寬骨長（`OKPhysXSecondary.LengthSlack`）、剛性骨長，結果不一致，換姿勢就失效。
- 原因：PhysX 用關節彈簧和軟骨長，動態本身就和 DynamicBone 不同；彈性很弱的瀏海對這差異最敏感。在 PhysX 參數上逼近原作是追不完的。
- 修正：新增後端 6「原作 DynamicBone＋碰撞」（`OKDynamicBoneStep`），並設為預設（`OKPhysXSecondary.Backend` 6）：
  1. 動態照原作：Verlet、彈性、剛性範圍、凍結平面、精確骨長。
  2. 原作每幀固定步長；這裡每幀只走一步，彈性、阻尼按 `1-(1-e)^F` 換算時間（F = Dt/參考步長）。第一版用多個子步，抖動很大。
  3. 碰撞沿用共用的精確修正，但只做投影（`bProjectOnly`）：不加接觸彈簧、容差與推出記憶，也跳過姿勢切換的 settle。若保留彈簧，粒子會在投影與彈簧之間來回彈，jitter 約 450。
- 量測（6 個姿勢，頭後仰 0°／50°）：pops 0，jitter 0–5，真穿透約 1.5 cm（Verlet 約 11 cm）；正面近拍瀏海與 Verlet 一致；龍尾巴（後背位）繞到男主背後，不穿身體。violations 0–600 是骨段中點（原作只投影粒子），不影響畫面。
- 預防：要「像原作」時直接用原作演算法，再補上原作沒有的東西（對方身體碰撞），不要拿別的引擎調參數去逼近。時間換算要逐幀處理，不要用子步。

### 分元件物理：量測時的兩個陷阱（2026-10-18）
- `pair_ctl.lua start` 遇到已在執行的場景會直接沿用：CVar（例如上一次的 `Backend 6`）和已載入的卡都不會重來。改卡片 `okPh` 後要先結束 E: 的 UnrealEditor 再 start。第一次以為卡片設定讓所有鏈變成 DynamicBone，其實是沿用舊的 `Backend 6`。
- 後背位 A_SLoop 的 pops/jitter 隨掉幀大幅波動：同一設定 10 秒內步數少的那次 pops 可到上千。比較解算器要看步數相近的幾次，並用 `bySolver` 分開看各解算器的鏈。結論：混用時 PhysX 鏈的 jitter（14–60）與全 PhysX（4–32）在同一雜訊範圍，DynamicBone 頭髮為 0。

### 尾巴辨識與彈性；頭髮做成飾品時誤用 PhysX（2026-10-18）
- 普查 102 張卡（Demo＋CrowdAPNG）的飾品 DynamicBone 鏈：
  - 尾巴有 18 張卡、22 條鏈。名稱有 tail、sippo、尻尾、Zhenzhen_Weiba（尾巴拼音）、acs_aku01_sippo（只有網格名稱有，骨頭叫 j_01…）。
  - 掛點都在腰：a_n_waist／waist_b／waist_f。
  - 頭上也有叫 TailL1 的鏈（C010 雙馬尾），所以「頭上的不算尾巴」。
- 普查發現的錯誤：原作卡常把整頭頭髮做成飾品（C050 `Yvonne_hair`，掛在 a_n_headside；C000 假髮）。分元件第一版把所有飾品都當 PhysX，等於 C050 的瀏海又回到 PhysX。改成逐鏈分類後，C050 的 DynamicBone 鏈步數從約 2,100 增加到約 41,000。
- 卡片鍵錯誤：session 的 source_property 是 `outfit0.accessory1.ChaFileAccessory.PartsInfo.id`，卡片和編輯器用的是 `outfit0.accessory1`，所以卡片對飾品的指定沒有生效。已在 `part_key` 正規化。
- 尾巴彈性第一版方向錯了：我把阻尼 ×0.35、擺動範圍 ×2。但原作尾巴本來就很鬆（彈性 0.09、剛性 0.10），再放寬只會更像麵條。改成加強拉回（彈性 ×2.5）、阻尼 ×0.5，範圍不動。側拍可以看到：TailBounce 0 尾巴甩到頭上，1／2 保持原形、跟著臀部動。pops、jitter、真穿透（1 cm）不變。
- 另：一個指令串裡 Lua 編輯斷言失敗，但後面的 git commit 不在同一條 `&&` 鏈上，仍然提交了沒有文件更新的版本。文件改用另一個提交補上。
- 預防：
  - 調「彈性」前先看原始參數。彈性是「回彈力＋低阻尼」，不是「更鬆」。
  - 分類規則要先普查實際卡片再定，並把普查到的名稱寫進單元測試（`px::component_tests`）。
  - 編輯和提交要放在同一條 `&&` 鏈上。

### LOL Ahri 狐尾：粗尾巴穿進自己與男方身體（2026-10-18）
- 測試：`D:\KoikatuSunshine\UserData\chara\female\LOL` 13 張卡，姿勢「插入・後背位」。
  - 尾巴都是 `o_acs_foxtail`（掛在 a_n_waist_b，5 顆粒子），每張卡 1 或 9 條。
  - 卡上設定很鬆：彈性 0.10→0.01、剛性 0.31→0.02。
  - 網格很粗：骨頭周圍半徑 4–14 cm，但卡片設定的 DynamicBone 半徑接近 0。
- 原因：
  1. 對自己的身體只用卡片設定的半徑（這原本是為頭髮靠在身上設計的）。粗尾巴翻到自己的頭和肩上，最深 7.9 cm。
  2. 稽核把骨頭子端的粗細套到整根骨頭，連父端都算。狐尾根部很細，男方的手剛好握在這裡，所以量出 5–7 cm 的「穿模」，但其實是量測錯誤：這時硬修正一次都沒觸發（`DebugHard`），畫面也正常。
- 修正：
  - 尾巴（旗標 32）對自己身體也用網格粗細，掛的那塊身體除外。
  - `OKPhysXSecondary.TailFluff`（0.6）：粗細的 60% 當實體，其餘 40% 允許彈性壓縮（使用者接受）。
  - `OKPhysXSecondary.TailCompress`（0.7）：尾巴骨長受擠壓時可縮到 70%，然後彈回。
  - 尾巴稽核的粗細沿骨頭由父端線性插值到子端；新增 `tail ... seen=最深/超過 2 cm 次數`，以及依身體部位分的 `t<n>`／`tp<n>` 統計。
- 結果：9 張有尾巴的卡，尾巴超出允許壓縮的穿入都在 1.7 cm 以內，超過 2 cm 的次數是 0；尾巴 pops 0–8、jitter 0–23。
- 另外查到兩件事：
  - 後髮不見：髮型 id 0 被跳過，見 CHARACTER_EDITOR_ERRORS。
  - KK_298455 是男性卡（sex 0），放在 female 資料夾。原本錯誤訊息是空的，現在會說明「這張卡是男性角色」。
- 預防：
  - 量穿模時，粗細要用網格的實際形狀（會漸細），不要用骨頭末端的粗細套整根。
  - 量測數字不合理時，先確認求解器有沒有出手（`DebugHard`），再決定要改量測還是改物理。
