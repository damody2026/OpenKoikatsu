# Unreal Animator：參數進入完整逐幀播放（stage291）

## 根因與修正

`OkSession::apply_shape` 已把 `anim_parameters` 傳入 evaluator，但 `apply_anim_frame` 仍呼叫無參數的 `apply_clip_at`，重新套 controller default。因此 Gallery getter、有效長度與原生 raw evaluator 的數值都正確，逐幀骨架仍沒有使用指定混合值。

逐幀入口改用 `apply_clip_at_parameters` 並傳入同一個 Session 參數表。C++ `FOkSession::SetAnimClip` 成功選取來源後統一清除 Open 的 external／T-pose mode；失敗不改模式，catalogue、mod、hand、非 Actor caller 共用這個入口。這個入口供所有角色卡、controller state、1D／巢狀 1D／Cartesian／巢狀 Cartesian 共用；不按角色名、骨名或截圖配置修姿勢。其後保持既有 helpers、ABMX、Ver02、衣物、頭髮與飾品發布順序。

Stage289／290 的原生 raw weights／duration／TRS 比對仍有效。原先有限值、getter 與 UI callback 的結果，不能證明參數已進入完整逐幀姿勢；本次補上實際完整流程的證據，重出受影響的 RHI 截圖。

## 驗證方法

1. `animator_parameters_reach_session_palette` 從 stage289／290 保存的原生五卡 fixtures 讀參數與時間。每卡 60 個 1D／巢狀 1D，加 144 個純 2D／巢狀 2D，共 1,020 frames。
2. 實測路徑經 FFI 選 state、設定 float、逐幀播放。控制路徑使用已經通過 Unity 原生數值驗證的 raw evaluator，獨立把 local TRS 送進 external Animator 入口；兩條路都走 helpers／物理／附件。比較所有 body transforms、runtime accessory vertices，容差 2e-5，另要求每卡超過 100 個非 default raw Animator 姿勢；此 positive 計數在物理前，不能當成每個 frame 的最終物理 pose 都改變。
3. Unreal `AnimatorParameterPropagation` 在五卡各測 Speed、siriL_X、muneR_X。經共用 SetAnimClip 自行進入來源模式、隔離 particle history 後要求 exported skeleton 確實改變，再切回原值要求完整姿勢恢復；此隔離測試不替代前述包含物理的完整 Session 比對。
4. Gallery 真實 source／state／slider callbacks 另外比較畫面使用的 `UPoseableMeshComponent` 骨架。1D 保持物理開啟；Cartesian 選取的 cf_j_siri_L 是 Ver02 控制骨骼，反編譯 `DynamicBone_Ver02.InitTransforms` 在 Animator 後會用 reference pose 覆寫它，因此此 positive control 暫時隔離物理、要求 X／Y 各自改變 local 與 world 顯示骨架，再恢復原設定與真物理姿勢後截圖。全程保留 normalized phase、face clock。完整 Session 和 RHI gate 一直包含物理，沒有為了通過 UI gate 改 production 順序。
5. 真 RHI 分別出 1D 和 Cartesian 五卡各 15 張圖，核對附件、camera fit、眼睛 audit 與背景。

修正前失敗保存在 `Saved/Logs/AnimatorSession291BeforeFix.log`：March／Locomotion_Anger／phase0／cf_j_sk_01_03，最大分量誤差 0.059407875。這是完整流程的負對照，沒有刪除失敗紀錄或放寬容差。

另有 `animator_cartesian_publisher_diagnostic` 驗證 Ver02 reference writer 在 dt=0 覆寫 Siri channel，而關閉物理時同一 X 參數會改變 exported rotation；結果保存在 `AnimatorCartesianPublisher291Diagnostic.log`。這只是來源順序與發布通路的負對照，不代表完整 HAnime 初始化／動態骨骼 enable 政策已支援。

## 重現

在 `C:/portable/OpenKoikatsu/Tools/ok-extract`：

```powershell
$env:OK_GPU_SKIN='1'
$env:OK_SKELMESH='1'
cargo test --release --lib animator_parameters_reach_session_palette -- --ignored --nocapture
```

編譯 Unreal 後在專案根目錄：

```powershell
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator_session.ps1' -Mode Tests
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator_session.ps1' -Mode Shots -Pose Layer -Cards All -Blink open
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator_session.ps1' -Mode Shots -Pose OneD -Cards All -Blink open
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator_session.ps1' -Mode Gallery -Pose Layer
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator_session.ps1' -Mode Gallery -Pose OneD
C:/portable/OpenKoikatsu/Tools/lua/lua.exe 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/verify_animator_session.lua'
```

完整 native Session gate 已通過：五卡各204 frames、188 nondefault raw positive frames；共1,020 frames／940 raw positive frames，610,980 body transform比較及22,132,164 runtime附件vertex比較，最大分量誤差均0，tolerance仍2e-5，耗時457.67秒。

正式 log、audit、截圖與 contact sheets 保存在 stage291 專用路徑。Gallery output directory 可由 `OK_VERIFY_ANIM_OUTPUT_DIR` 指定，避免重跑新版 callback gate 覆寫 stage289／290 UI 證據。

## 最新產物

- `Saved/DebugShots/Animator291_OneD_open`：五卡15張，Locomotion_Anger／Speed=.37，包含全身、正臉與側臉。
- `Saved/DebugShots/Animator291_Layer_open`：五卡15張，authored base Idle躺姿＋單個Cartesian Override；保留source物理，不稱一般走路或所有HAnime互動初始化。
- `Saved/Tests/Animator291/ContactSheetOneD.png`、`ContactSheetLayer.png`：兩批總覽。
- `Saved/DebugShots/Animator291UI_OneD`、`Animator291UI_Layer`：Gallery callback截圖。
- `Saved/Logs/Animator291Audit.json`、`Animator291Verified.log`：完整gate及source／fixture／log／image SHA256。

## 範圍

這次修正完整參數播放通路，沒有新增 automatic transitions、CrossFade、同時多個 weighted layers、additive、Directional 2D、IK 或 mirror。下巴輪廓线依使用者要求暫不處理。來源 layer／transition 邏輯仍應沿實際 controller 與 `ChaControl`／`AnimatorLayerCtrl`／`Studio.OCIChar` 載入順序補齊，不能推定選一個 state 就涵蓋整個遊戲 Animator。

## 最終驗收（2026-09-29）

Stage291全部正式gate通過，`Animator291Verified.log`為PASS。Unreal15個distinct tests全部Success／QueueEmpty15／exit0，包含五卡15個參數變化及完整恢復控制、450個真1D來源影格、300個真Cartesian來源影格，source／external乳搖負對照各900影格，以及手勢、mod catalogue播放、嘴型、眉眼、時鐘、視線與sparse／invalid外部動作回歸。

兩組RHI共30張900×1080，逐張目視；每組71附件audit誤差0cm、15camera fits≤.92與眼睛／視線audit通過。1D Gallery實際display變化642骨骼；Cartesian隔離控制X變7個world／1個local、Y變9個world／3個local，合計16個world變化，兩個callback都有效，phase／face clock保持，physics恢復後才截圖。另獨立reference writer控制通過，關物理時export quaternion最大變化0.053007465。

初版build26.06秒、test-mode補正build9.48秒、共用入口及Gallery最終build12.08秒均成功。30張RHI及1D UI由第一個build產生，其Actor入口本已清source mode；最終共用入口改動不改Actor的數值結果。最終15項tests與Cartesian UI使用最終build，未將不同build證據混稱同一輪。最初failed queue／Gallery、後續explicit test-mode accepted queue與reference診斷皆保留在stage291 log／old子目錄。

## Stage292：速度參數補齊

後續已補 serialized m_SpeedParamID 的 current值，single clip／BlendTree／explicit Override共用 clock。MotionSpeed與speedHand原生180frames、完整Session180frames、UE16tests與真Gallery／15RHI驗收見Docs/UNREAL_ANIMATOR_CLOCK.md及HANDOFF2 §149；stage291的參數傳遞修正保持。完整HAnime初始化與其他graph限制仍見各章範圍。
