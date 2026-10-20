# Stage376 原作共用動畫與獨立嘴型（播放與嘴型回歸通過）

需求：原作動畫只匯入一次成共用資源；不同卡片依自己的骨架、衣服及飾品資料求解，同時保留可獨立控制的嘴型與開合。已有角色專用 exact palette 動畫不能直接冒充跨卡共用動畫。

實作順序：
1. 保存原始 streamed/dense/constant 曲線與 Avatar TOS，新增版本化、限量、拒絕尾隨資料的記憶體載入接口，不改既有 portable wire layout。
2. 角色 outfit 保存無遊戲路徑的求解 snapshot；共用曲線資產不引用角色資產。播放時計算各角色自己的完整 palette。
3. 原作 FBS 嘴型配對、基準權重及隱藏口腔部件一併匯入；嘴型用百分比截斷並扣除已烘焙基準，獨立疊加骨架變形。
4. 提供共用資源匯入及 actor 播放 API，驗證兩種角色引用同一資產、獨立時鐘、seek、暫停、嘴型，以及無 KKS 檔案播放。
5. 重跑既有匯入目標保護及骨架回歸。所有新輸出使用 fresh tag，舊 seal 不重寫。

目前限制：完整 controller/blend graph 不能默默降成單 clip；未支援時明確拒絕。本輪涵蓋男女行走 raw clip 與兩張 outfit0；未宣稱全部 controller、全部卡片或 Shipping 已驗收。

## 使用方式

Unreal 的 Tools 或 Content Browser Add 選單開啟 **Import Original KKS Skeletal Animations...**。填原卡路徑讓工具定位已匯入的共用來源，或填 Resource root；指定 source bundle、精確 clip 名稱（介面用分號分隔）及全新 `/Game/...` 目的地。按 **Import Shared Original Animation Resources**。

新增的 `UKKSharedAnimation` 保存 authored streamed/dense/constant 曲線與 Avatar TOS，不引用 Character。它由 `AKKImportedCharacterActor` 播放，並非角色專用 `UAnimSequence`；既有 bake 按鈕仍保存後者。

放置 `KKImportedCharacterActor`，設定 Character、Outfit、Shared Animation。同一個 Shared Animation 可以指定給多個 actor；各自保留時鐘與求解 session。藍圖可呼叫 `PlaySharedAnimation`、`SetAnimationPosition`，或修改 `SharedAnimationPlaying` 暫停／恢復。

藍圖 `SetMouthControl(Pattern, Open)` 使用原作嘴型索引和 0–1 開合；播放或 seek 不會清除它。`ResetMouthControl` 回到匯入基準；可設定 Mouth Open Curve 在動畫相位上驅動開合。骨架與嘴型可同時運作，嘴型不改骨骼姿勢。

舊 Character 必須重新匯入，才能包含無遊戲路徑的 AnimationSnapshot、FBS 配對及閉嘴時隱藏的口腔部件。共用播放需要 KKUnityBundle runtime evaluator 外掛，資產保存後不需要 KKS 安裝／原卡／zipmod；舊 baked 動畫仍可只用 KKImportedAssets。此版本 snapshot 保留原有 snapshot 資料（包含圖片），尚未作儲存體積最佳化。

命令列共用匯入：`-run=KKImportSkeletalAnimations -Shared -GameRoot=<來源> -Source=studio/anime/00.unity3d -Clips=f_aruki_00_00_01,mc_m_walk_00_00_01 -Destination=/Game/<全新目的地>`。不需指定 Character 或 Card。

## 驗證結果

- 原專案完整 Development Editor BuildR10 編譯通過；Unreal 关闭後完成 DLL 連結。失敗及中止的舊 runs 不列入通過。
- SharedR3 保存女性與男性 walking 共 2 份資源，character_references=0；FemaleR3 匯入 59 部件，MaleR1 匯入 21 部件。
- RuntimeFemaleR7／RuntimeMaleR4：兩個 actor 引用同一資源，驗證全部 renderer 的 affine palette、回跳、跨循環、暫停及各自嘴型。最大 matrix 元素誤差為 0.000208616／0.000245869，原 0.01 門檻不變。這是 palette 對照，非全頂點原生距離審查。
- 口部實際頂點開合位移為 0.854886591／0.911629379 公分；嘴型不改骨骼姿勢。閉嘴隱藏口腔、張嘴顯示口腔也驗證通過。
- DetachedR2（最終 BuildR10 DLL）在沒有 Content/IndependentResources、Tools、原卡或 source root 的隔離專案，男女兩張不同卡片共用同一份女性行走資源。80 個 skeletal renderer、獨立時鐘／嘴型、嘴型曲線實際 morph 權重、loop／seek／pause、共用 payload 不變均通過。仍需 KKUnityBundle runtime evaluator 外掛；不等於只安裝 KKImportedAssets 即可播放此共用格式。
- Rust 原作男女曲線重開取樣一致；損壞／尾隨資料／非有限值拒絕、Unity Infinity stepped-key 支援通過；既有 APNG portable snapshot 相容測試 LegacyR1 通過。
- Stage375 匯入目的地 Production376R1 與 ExactPaletteMath Stage376R1 回歸通過。既有 baked OriginalAnimations 的隔離回歸 Stage376R1 也通過：4 clips、148 skin instances、363234 vertices。
- 男卡即時來源新增隱藏部件後索引會變；對照 oracle 依 renderer、role、v2 hash 唯一匹配，缺失或多候選拒絕。runtime 仍核對自己的已保存 snapshot 索引與來源雜湊，不用單卡補丁。
- 完整失敗原因與修正見 ERROR_LOG E253／E254。Automation TestExit 可能在 Fail 時回傳 0；runner 同時要求 Success 及 valid=1 coverage marker。

後續 Stage377 修正：共用資產 Details 可編輯 Loop。骨架及 MouthOpenCurve 共用 GetPhase，非循環都停在同一結尾相位；共用播放不再重複輸出 CPU accessory 頂點。男女站立與行走／one-shot 回歸及詳細範圍見 [SHARED_ANIMATION_PHASE377.md](SHARED_ANIMATION_PHASE377.md)。
