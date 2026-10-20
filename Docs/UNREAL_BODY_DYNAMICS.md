# Unreal 身體物理與乳搖

## 執行流程

來源／mod AnimationClip 或 controller state → Animator取樣 → ShapeBody.UpdateAlways
→ 場景ExpressionBone與ABMX → DynamicBone_Ver02 → 共用衣物DynamicBone
→ shared GPU骨骼palette → 私有髮與配件 → Unreal顯示。

卡片身體大小、柔軟度、重量來自ChaFileBody；不依角色名設定振幅。
Ver02從實際body prefab抽取root、Bones、Patterns、RefTransform、碰撞與限制，
與普通髮／裙DynamicBone分開。粒子歷史保留在session記憶體，cache只存設定。
動畫時間可以循環、停在同一sample或seek，物理使用真正的frame delta。

## 反編譯來源

- `C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/DynamicBone_Ver02.cs`
- 同目錄的`BustSoft.cs`、`BustGravity.cs`、`ChaControl.cs::UpdateBustSoftnessAndGravity`。
- `BustSoft`：rate=clamp(softness*size+.01)；Damping [.2,.1,.1]、
  Elasticity [.2,.15,.05]、Stiffness [1,.1,.01]依rate分兩段內插。
- `BustGravity`：Y=-.005*clamp(weight)*size*softness*.5。
- Ver02 LateUpdate先從live reference bones還原，再推粒子、限制、碰撞與骨骼。
  已安裝IllusionFixes停用SkipUpdateParticles時，沿同一來源policy分流。
- KKS_Colliders的Studio／card flags決定自體手臂／地板，並以當幀真骨矩陣更新。

主要程式為`Tools/ok-extract/src/dbone/ver02.rs`、`session.rs`與
`Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp`。

## Stage287：一般走路缺口

一般走路按鈕過去只在UE顯示骨架上套rotation，沒有經過來源物理。
現在無`OK_WALK_ANIM`外部資產時，使用真正Studio controller state：

`states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01`

這與動畫目錄載入共用SetGameAnimation／ApplyAnimFrame，包含body、衣物與附件
更新；Gallery保留這個來源key，換卡後延續。顯式mannequin重定向仍是另一條
既有路徑，不以本次結果宣稱它具備來源乳搖。

## 驗證

- UE `OpenKoikatsu.AnimationCatalog.BodyDynamics`：五卡各180幀，共900幀；
  sample time跨循環，dt固定1/60。讀兩側六節胸骨，比對同phase跳過物理的
  負對照，證明物理改變pose。負對照dt=0且不更新粒子歷史。
- Shot display audit：呼叫一般走路入口，確認切到來源PoseMode2；180幀中
  真正body與所有GPU followers的六節胸骨palette一致且finite。
  每卡保存1080列display trace與正面、臉、側臉三張圖。
- 既有原生controlled-chain fixture驗證Ver02數學，包括120Hz累積時間、
  已安裝SkipUpdate修正及arm collision。它是solver隔離控制，與UE顯示gate
  分開紀錄，不能冒充完整動作的遊戲逐頂點對拍。

```powershell
& Tools/GamePoseProbe/run_body_dynamics.ps1 -Mode Tests
& Tools/GamePoseProbe/run_body_dynamics.ps1 -Mode Shots -Pose Walk -Blink open
Tools/lua/lua.exe Tools/GamePoseProbe/verify_body_dynamics.lua
```

## Stage287最終結果

UE BodyDynamics測試Success／QueueEmpty1／exit0。五卡各180幀，包含跨clip
循環；以下是與同phase停用物理的六節來源胸骨差異，單位公分／弧度。
這些是實測值，不用作角色設定或生產分流。

| 卡片 | 最大位移 cm | 最大旋轉差 rad |
| --- | ---: | ---: |
| March_7th | 0.181548457 | 0.062871525 |
| Alice | 0.085508181 | 0.028261556 |
| Yuuka | 0.085875173 | 0.030020645 |
| KK_091207 | 0.020781570 | 0.004833739 |
| KK_384283 | 0.120616373 | 0.044388873 |

實際顯示各180幀／共900幀，GPU followers合計57,240次骨骼比對，
位置誤差0cm且完整transform Equals(1e-6)通過。五份trace各1080列finite。
原生120Hz controlled fixture十骨local position最大1.1e-7m，
quaternion component最大8.2e-7，保留原嚴格門檻。
15張900×1080截圖已全部目視，71個髮／配件頂點audits誤差0cm。

完整機器驗收：`Saved/Logs/BodyDynamics287Audit.json`／
`BodyDynamics287Verified.log`；test console為`BodyDynamicsUnrealAutomation287Console.log`。
實際顯示trace與總覽在`Saved/Tests/BodyDynamics287/`，
截圖在`Saved/DebugShots/BodyDynamics287_Walk_open/`。
production build53.29秒成功，test selector修正後重鏈結2.95秒成功。

一般走路按鈕及動畫目錄的原生／mod clip都經相同source物理流程；
卡片／bundle設定決定骨鏈与參數。已測五卡不等於窮舉所有mod資源。
顯式mannequin重定向、完整Animator graph、任意第三方physics override及
跨角色碰撞維持既有範圍限制；本次未處理下巴輪廓。

## Stage288：外部 Unreal 動作也使用來源物理

`AKKPreviewActor::SetUnrealAnimation(UAnimSequence*, bool bLoop)` 為Blueprint入口。
`PlayAnimSequence`與`StartAnimLoop`也沿此共用播放流程；明確`OK_WALK_ANIM`
資產找不到會回失敗，不再生成近似walk冒充載入成功。來源遊戲動作沿原入口。

外部動作 → 從card-shaped Animator baseline取樣／重定向 →
ShapeBody.UpdateAlways／hand／場景helpers → ABMX → Ver02 → 衣物 →
同一GPU palette → private髮／配件。每frame都從未模擬的baseline開始，
不把上一幀物理pose當neutral；也不在發布palette之後再overlay衣服骨頭。

### 姿勢與座標契約

- `OkBodyPose`是source body tree的local TRS，Unity公尺、XY Z／xyzw。
  index使用同session export。提交整幀驗證後才更新pose/history；可只提供部分
  transforms，其餘維持卡片baseline。完整body tree可包含動畫驅動的helpers，
  不以cf_j_／shape layer分類假定Animator可控制哪些骨頭。
- `ExternalBodyPoseAt`第一次建立形體基準，之後不推進physics clock。
  不接受head-only bones、locked synthetic root、未知／重複index、
  NaN／Inf、零quaternion或零scale。已unit的source float quaternion保留原值，
  避免微小重正規化擾動長碰撞鏈；其他非零quaternion才正規化。
- Unity與UE基底為B(x,y,z)=(-x,z,y)，position乘／除100，scale交換Y/Z，
  quaternion做basis conjugation；不以world矩陣重分解多次破壞非等比形體。
- UE sampler保留missing tracks的baseline；完整三軸local rotation用source與
  target reference component frames轉換，人體左右／上下方向對齊，位移轉成
  target parent frame並依stature比例，scale乘卡片baseline。exact KKS名字優先，
  其他使用humanoid對照。沒有source rest geometry時無法推定解剖basis，使用
  identity alignment；任意非humanoid skeleton需提供對應的retargeted body locals。
- 外部UE動作沒有Unity來源場景，scene auto使用Studio preview，明確場景優先。
  身體／衣物／附件的collider policy都使用同一當幀場景。

### 驗收與重現

來源數值控制把真Studio walk的Animator output經兩條入口送入相同source
helpers與物理：五卡900幀、539100個body transform samples、19528380個
髮／配件vertex samples，position／quaternion／附件vertex差異全部為0。
30個invalid frame案例均拒絕，並核後续particle history仍與來源一致。
`Saved/Logs/ExternalBodyRust288Parity.log`為正式Rust來源證據。

UE診斷使用獨立、具humanoid reference geometry的UAnimSequence fixture，
包含骨盆位移、三軸spine／neck／head旋轉；不是遊戲逐頂點對拍或production fallback。
fixture本身先驗21條tracks與quarter pelvis Z=94cm，negative control另驗跨frame
hip變化，防止恆定錯誤pose誤過關。五卡physics負對照、全GPU followers、
轉頭時髮／配件、missing track及正／負非等比scale座標roundtrip分別驗收。

```powershell
Push-Location C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib external_animator_matches_native_cards -- --ignored --nocapture
Pop-Location
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_external_body.ps1 -Mode Tests
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_external_body.ps1 -Mode Tests -ExternalOnly
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_external_body.ps1 -Mode Tests -SparseOnly
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_external_body.ps1 -Mode Shots -Blink open
C:/portable/OpenKoikatsu/Tools/lua/lua.exe C:/portable/OpenKoikatsu/Tools/GamePoseProbe/verify_external_body.lua
```

### Stage288最終結果（2026-09-29）

已完成外部UE動畫入口與實際渲染驗收。前述stage287「外部mannequin尚未
接物理」是當時狀態，現已由本節取代。身體、衣服、側髮與配件都在
外部Animator pose之後更新，使用同一來源時序，沒有按卡名修正。

主build的11項regression均Success；最終build另重跑ExternalBodyDynamics與
ExternalSparseTracks兩項Success／QueueEmpty2／exit0，共12種測試。
最後只改診斷motion的雙臂位置，便於檢視側髮與頸飾；reference geometry
及production retarget不變。review重建4.66秒Succeeded；收尾另補無效軌道
preflight（零scale／零quaternion／非有限TRS與無效reference），最終重建
5.79秒Succeeded。ExternalSparseTracks擴充並重跑Success／QueueEmpty1／exit0，
真實fixture零scale會在切換actor／重設particle history前被拒絕。

外部fixture五卡各180幀，負對照停用物理、保留相同phase；以下為來源
兩側六節胸骨的差異。它們是測試輸出，不是生產參數。

| 卡片 | 最大位移 cm | 最大旋轉差 rad | 骨盆動作 cm |
| --- | ---: | ---: | ---: |
| March_7th | 0.358664443 | 0.167065025 | 2.179579491 |
| Alice | 0.208420429 | 0.102025625 | 2.147471760 |
| Yuuka | 0.221654046 | 0.117490909 | 2.115363994 |
| KK_091207 | 0.158732830 | 0.095649939 | 2.107329273 |
| KK_384283 | 0.304805628 | 0.156611639 | 2.227756678 |

實際RHI顯示900幀、57,240次GPU followers骨骼比對，完整transform
Equals(1e-6)通過、位置差0cm。五份trace各1080列finite，跨frame確有位移。
五卡全身／臉／側臉共15張900×1080圖已目視，轉頭時側髮、髮飾與頸飾
未見新增分離；71個附件vertex audits差0cm、15個fit≤.92。
缺少source arm track保留卡片baseline，正／負非等比scale座標roundtrip通過。
最終input-guard build另重新输出全部15圖及900幀display audit；
review build清楚但尚未含guard的圖保留`old/BeforeInvalidTrackGuard/`。

- 完整機器結果：`Saved/Logs/ExternalBody288Audit.json`、`ExternalBody288Verified.log`。
- 來源入口等價：`ExternalBodyRust288Parity.log`（900幀／539100骨／19528380附件頂點全0）。
- 最終UE負對照：`ExternalBodyFinalUnrealAutomation288Console.log`。
- 最終invalid-track preflight：`ExternalBodySparseUnrealAutomation288Console.log`。
- 實際渲染：`ExternalBodyShots288_open.log`，trace與總覽在`Saved/Tests/ExternalBody288/`。
- 本次15圖：`Saved/DebugShots/ExternalBody288_open/`；舊受手臂遮擋的圖在`old/ArmsOccludeFace/`。

`SetUnrealAnimation`可在Blueprint或C++指定真實`UAnimSequence`並選擇循環。
`PlayAnimSequence`沿相同入口，`StartAnimLoop`保留當前選定動作。
需要自訂非humanoid映射時，可先重定向成同session的Unity body-local TRS，
經`apply_external_body_frame`提交；physics dt應傳真實frame delta，而非clip循環相位差。

#### 截圖資料修復紀錄

288 runner初稿漏改output expression，外部圖曾覆寫287的來源走路圖。
已修正獨立目錄、保留錯誤capture log與hash manifest，並重新產生五卡
來源走路15圖；`BodyDynamics287RecoveredVerified.log`完整PASS。
重新產生的PNG雜湊與原圖不同（0/15相符），不宣稱原檔已逐位元復原。
原audit／log／ContactSheet保存在`BodyDynamics287Before288RecoveryAudit.json`、
`BodyDynamicsShots287Before288Recovery.log`與`ContactSheetBefore288Recovery.png`；
`Saved/Tests/BodyDynamics287/Recovery288.json`逐檔記錄差異。
目前287 audit明確帶有recovery欄位，外部最終15图直接寫入正確288目錄。

本節驗證humanoid／KKS重定向與來源物理時序，不等於UE診斷fixture與遊戲
逐像素或逐頂點動作一致。任意第三方physics override、跨角色碰撞與完整
Animator graph仍是獨立工作。下巴繼續延期。
