# Animator motion 時間倍率（stage296）

## 根因與來源

原讀取器遇到 motion leaf 的 `m_Duration != 1` 會拒絕整個 state。獨立 UnityPy 掃描匯出清單中的 437 個 base AnimatorController，找到 6 個實際設定：

| Bundle／controller | State／motion set | Leaf duration |
|---|---|---|
| `action/animator/00.unity3d`／`player` | `Base Layer.Main.Locomotion`／0 | run：0.8333333；walk：1 |
| `h/anim/female/05_01_00.unity3d`／`khr_f_02_00` | `Base Layer.OLoop`／1，同步 layer73 | 五個胸圍 motion：0.5 |

同一 H state 的 base motion set0 與同步 layer74 的 motion set2 皆為1。base OLoop 原本可載入；阻擋的是 layer73 的共用 graph 組合。不能從所有 tree 的掃描結果推論 base tree 被拒絕。

來源圖層設定、PPtr、參數 ID／TOS、motion set index 決定載入方法。沒有角色卡名稱、臉型或骨架比例分支。掃描明細與 bundle SHA256 在 `Saved/Tests/MotionTimeScale296/SourceAudit.json`；此掃描限 base controller，不宣稱所有 mod controller 都有相同欄位值。

原生 probe 使用真正的 `ChaControl.LoadAnimation` 與 Unity Animator：Rebind、指定來源 floats、清除其他 active layers、Play normalizedTime0、Update(elapsed)。物理只在獨立 raw Animator 控制中停用；Session／Unreal 正式流程保留物理。每筆保存來源 clip weights／原 clip 長度、state length／normalizedTime／speed／speedMultiplier、base clock，以及實際 Animator 子樹的 CF local TRS。

## 已實作的共用規則

- leaf 保留來源 `duration_factor = m_Duration`，僅接受有限正值。它是 clip 長度倍率：原生 run 的0.8000001秒 ×0.8333333＝0.6666667秒；不是把0.8333333直接乘 state speed。
- BlendTree 有效長度為 `Σ(weight × raw_clip_length × duration_factor)`，再除 source state 的 effective speed。各 child 仍用共用 normalized phase 取樣自己的原 clip 長度，不能再乘一次 duration factor。
- 同步 layer 的 `m_SyncedLayerAffectsTiming=false` 保留 base state 的時鐘與 normalized phase。layer73 的0.5不能把 base clock 也縮一半；原生 layer73／74控制共同驗證此邊界。Timing=true仍未支援。
- 單 leaf 有非1的倍率時保留 MotionNode，走同一 parameter-aware evaluator。state cycle 與 leaf cycle 各只套一次。公共 raw default-clock sample 則將 speed 除 duration factor，再合併 leaf cycle，避免只修 BlendTree而漏掉 single-clip API。
- serialized/runtime override 只替換來源 clip 身分；duration factor 留在 motion上，不能隨新 clip 丟掉 authored clock。
- Blueprint／Gallery／FFI沿用來源 state與float入口；無新 ABI 或 card cache schema。graph快取是程序內資料，重啟新binary重新讀；既有 pipeline141／decoded90的 card cache 已用五卡實際載入驗證。

## 通用 graph 與來源場景

第一組真RHI目視發現手腳被拉成長線，即使raw clock／TRS與兩個相同Studio流程的Session比對都過關。根因是沿用`studio@`來組合game H controller，加上runner強制scene4；所有composed graph因此被視為Studio，插入Studio PVCopy與ExpressionBone，與game H motion的來源關係不符。

新增`controller@`描述符：同樣四欄UTF-8 JSON→HEX（base source、base owner、overlay source、overlay owner），以共同graph loader解析motion sets與同步base state；source scene遞迴沿base來源決定。`[Controller layer N]`標籤與`controller:`cache key獨立於舊`[Studio layer N]`／`studio:`。`studio@`仍供真正OCI Studio初始化；Studio profile API拒絕普通controller descriptor。

來源`ChaControl.LateUpdateForce`使用Manager.Character的arm／hand correction flags；`HSceneProc`與`EstheticProc`關閉hand correction。Studio的ExpressionBone與PVCopy來自Studio角色建構／Prepare，不能因為需要同步graph而套到所有動畫。新Auto H使用既有H/esthetic規則；normal action使用normal，無卡片特例。明確scene override仍可由caller選擇。unit確認兩種來源場景與cache分離，raw native與Unreal對照亦驗Auto。

初版15張與其Session／UE logs封存在`Saved/DebugShots/old/MotionTimeScale296BeforeSourceContext`及`Saved/Logs/old/MotionTimeScale296BeforeSourceContext`，不算accepted。最終五卡新RHI全部手腳恢復正常；這個缺口已補入正式共用載入方法，沒有在正式畫面關閉乳搖或套角色補償。

## 驗證

五卡：March、Alice、Yuuka、KK_091207、KK_384283。

每卡60個原生影格：player 的 Speed0／.37／1 × MotionSpeed1／1.6 × elapsed0／.17／.61／.91，共24；H同步layer73／74 × Breast1的0／.25／.37／.5／.75／1 × elapsed0／.17／.61，共36。H height=.37、height1=.61。對照包含 scaled與unscaled leaf端點、混合值、跨loop與非1 state speed，沒有借用不同Session的palette索引。

原生300frames／81,000 animated TRS attributes、80個scaled-clock負對照、180個同步base-clock控制已PASS。position／quaternion／scale最大誤差分別1.1920929e-7／3.5762787e-7／1.1920929e-7，原2e-5 gate不變。player clips命名雖含male，驗收項是實際CF binding與原生CF子樹，沒有僅憑clip名字宣稱retarget或忽略pose比較。

完整Session以獨立external-body入口接入已native驗證的raw locals，保留來源場景的helpers／ABMX／乳搖／衣物物理及private附件。來源Auto不在normal/H插入Studio PV／ExpressionBone；external對照明確提供known normal2／H3場景，因外部UE曲線的Auto原契約預設Studio。300frames／180,000骨TRS／6,509,460附件vertices已PASS；骨分量max5.9604645e-8、附件max0m。初次test漏建shaped baseline、後來external Auto場景不一致的失敗分別保留FirstRun／ContextFirstRun。原生在失敗run裡獨立PASS；最終仍需以正確source context的兩個test共同PASS驗收，不能把一過一敗或舊Studio版Session通過說成最終全綠。

scaled single leaf／cycle／invalid0、負值、NaN、Infinity的parser邊界unit已PASS。Unreal新test五卡300frames／80scaled clock／180sync clock通過，318個actual published bones有時間變化；另1D／Cartesian／state clock／Studio source initialization四項回歸已PASS，五distinct UE tests合計1365 source frames；没有重跑未受影響的全19／20項queue冒充必要gate。四個controller units亦PASS。

## 重現

```powershell
# workdir C:/portable/OpenKoikatsu
Tools\lua\lua.exe Tools/anim_export/inspect_motion_time_scale.lua
& Tools/GamePoseProbe/build.ps1
& Tools/GamePoseProbe/run_motion_time_scale_native.ps1 -Cards All
# workdir C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib native_motion_time_scale -- --ignored --nocapture
cargo test --release --lib motion_time_scale_session -- --ignored --nocapture
cargo test --release --lib scaled_single_motion_preserves_offsets_and_rejects_invalid_duration
# build Unreal後，workdir C:/portable/OpenKoikatsu
& Tools/GamePoseProbe/run_motion_time_scale.ps1 -Mode Tests -MotionOnly
& Tools/GamePoseProbe/run_motion_time_scale.ps1 -Mode Tests -RegressionOnly
& Tools/GamePoseProbe/run_motion_time_scale.ps1 -Mode Shots -Blink open -Cards All
Tools/lua/lua.exe Tools/GamePoseProbe/verify_motion_time_scale.lua
```

正式RHI在 `Saved/DebugShots/MotionTimeScale296_Sync73_open`：source controller composed H layer73、elapsed.4、嘴8/.8、blink open、gaze(.6,-.4,-.4)、Scene Auto依base H來源判定H/esthetic。這是明確指定來源 graph與來源場景的顯示驗證；並非 `OCIChar.LoadAnime` 全自動scheduler、該H的全部PV／Yure初始化、T-pose或walk。原生callback時序與shader像素等價不由本輪證明。根目錄stage286中性T-pose參考保留；下巴依使用者要求延期。

## 尚未涵蓋

完整多active weighted layers／Additive、FK／IK solver、transitions／CrossFade、mirror／foot IK、零與負時間倍率、同步Timing=true、所有mod graph与完整shader像素對齐仍需各自實作和來源驗證。本輪修正的是正值motion leaf時間倍率，不擴大宣稱全部遊戲動作已完成。

## 最終驗收（2026-09-29）

正確controller source context版Rust兩項共同PASS，130.35秒：native300frames／81000attrs；Session300frames／180000骨TRS／6509460附件vertices，max5.9604645e-8／0m。原2e-5門檻不變。最新production build24.38秒Success，五卡新UE test300frames／318 changed bones與四項1065frames回歸均Success、QueueEmpty各1／4、exit0。

新RHI五卡15PNG900×1080、71附件audits0cm、15fits≤.92、424followers samples與source→GPU骨palette max2.252e-6cm；五卡face time／gaze／eye highlight／world EyeLook gates全部PASS。PV display audit此處只核對四組綁定骨的palette與followers傳遞，metadata flags1111是保存的Studio policy；H來源scene不執行Studio PV，不以該flags宣稱H PV啟用。ContactSheet全部15圖與March全身、Alice正臉原圖已目視；手腳不再拉長，側髮、髮飾、頸飾未見新增偏移，眉眼保留。

來源H profile與Stage286中性Tpose的SHA256一起驗證；所有owned native與UE完成、临时probe不留遊戲plugins。SourceAudit／native fixtures、失敗logs、最終source／binary／PNG hashes統一存MotionTimeScale296Audit.json／Verified.log。錯誤E-152包含错误scene的目視拒絕，不把原第一组图列為accepted。
