# Unreal 來源 Animator 速度倍率（stage292）

## 根因與來源

原 controller reader 在 `state_entry` 把 `m_SpeedParamID` 的預設值乘進 `m_Speed`，之後不再保存參數身分。Gallery 無法選到 `MotionSpeed`；逐幀取樣與 duration 都固定使用載入時的倍率。來源判斷依 serialized controller 的 parameter ID／TOS 名稱，與卡片名稱無關。

遊戲原生控制使用實際 `CharaStudio.exe`／`ChaControl.LoadAnimation`／`Animator`。每筆重新 Rebind，將其他 layer 權重設零，播放相同 state 的 normalizedTime=0，再 `Animator.Update(elapsed)`；記錄 state length、speed、speedMultiplier、normalizedTime、clip weights 與 CF local TRS。原生物理只在 raw Animator 控制中停用；Unreal 與完整 Session 的正式輸出保留物理。

另已核對 `Managed/CharaStudio_Data/Assembly-CSharp/Studio/OCIChar.cs` 的 `LoadAnime`：HAnime 還有 Breast1／Breast／breast、breast layer、motion、PV、YureCtrl 初始化。此輪的速度參數不能代替那些功能。

## 共用實作

- `StateEntry.speed_parameter` 保存 source ID、名字與 default；既有 `timing.speed` 仍是預設倍率下的 clock，避免破壞 default-clock sampler。
- 每幀採 `effective_speed = timing.speed × current_multiplier / default_multiplier`。BlendTree 的有效長度為各 leaf 長度按來源權重加權，播放 duration 為此長度除以 effective speed；單 clip 與 explicit Override 也用同一路徑。Override 的 base state 用自己的 clock。
- 單 clip 帶速度參數會進入 parameter-aware pose evaluator；`load_clip_at` 仍可取其無歧義的 default-clock sample。真正多 motion 的 BlendTree 與 masked layer 不准偷偷取第一個 clip。
- `SetGameAnimationFloat` 保持 normalized phase，再以新的 duration 播放，setter不推進臉部時鐘或物理 dt。來源 blend 與 speed 參數可能同時存在，UI 與測試必須按名稱尋找，不能假設第0列是 Speed。
- Session setter 先檢查候選參數下選定／base state 的有效正向 clock 與有限正 duration，再提交；失敗保持参数、時間與既有 pose。此輪不提供零倍率 pause 或負倍率反向播放。slider 的正數範圍是預覽 UI 範圍，沒有聲稱 controller 序列化了 min/max。

## 驗證設計

五卡：March、Alice、Yuuka、KK_091207、KK_384283。`Base Layer.Main.Idle`（單 clip）與 `Base Layer.Main.Locomotion_Anger`（BlendTree）用 MotionSpeed=.4／1／1.6；另 `khs3_f_base3 / 05:Te_mune_D_momu_L.Te_mune_D_momu_L` 的 nested1D→Cartesian Override 用 speedHand=.2／.5／.8（source default=.5）。每種elapsed 0／.17／.43／.91，共180 frames。走路 blend `Speed=.37`；layer 的height=.37、Breast=.61、muneR_X=.31／Y=.79、base用authored Idle且global Animator speed=1。

1. Raw Rust 對 native state clock、duration、clip weights、animated TRS attributes；default multiplier=1 作負對照，非default在固定 elapsed 必須改變 pose。容差維持2e-5。
2. 真 FFI／Session 參數更新，對照已通過 native 的 raw evaluator經獨立 external-body入口，兩路共享 helpers、ABMX、body／衣物物理和private附件；比較全部body TRS及附件vertices。逐frame另驗0／負值／NaN／Infinity／造成duration溢出的最小正值，五卡共900次拒絕與原子性控制。
3. UE 五卡三state，180 source frames；固定 elapsed有實際pose變化，返回來源預設倍率還原全部pose，invalid clock保留duration／pose。既有1D／2D、參數、來源與外部乳搖、稀疏track、mod目錄、手勢與臉部測試回歸。新增speedHand後nested參數為5個，gate依名稱核對必要項，不能寫死舊的4列。
4. 真 Gallery source／state／blend／速度slider callbacks；倍率1.6後duration=.625倍、normalized phase與face clock保持、即時pose保持；再前進.23秒，實際display bones必須改變。
5. 五卡RHI全身／正臉／側臉15PNG，倍率1.6、elapsed .4，真物理與眉眼／視線保留；附件audit、camera fit、眼睛／gaze gate與目視檢查。

## 重現

```powershell
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/build.ps1
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator_clock_native.ps1 -Cards All
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator_clock_native.ps1 -Cards All -LayerOnly
# workdir C:/portable/OpenKoikatsu/Tools/ok-extract
$env:OK_GPU_SKIN='1'; $env:OK_SKELMESH='1'
cargo test --release --lib native_animator_parameterized_clock -- --ignored --nocapture
cargo test --release --lib animator_clock_reaches_session_palette -- --ignored --nocapture
# Unreal build之後，workdir C:/portable/OpenKoikatsu
& Tools/GamePoseProbe/run_animator_clock.ps1 -Mode Tests -Pose OneD
& Tools/GamePoseProbe/run_animator_clock.ps1 -Mode Shots -Pose OneD -Cards All -Blink open
& Tools/GamePoseProbe/run_animator_clock.ps1 -Mode Gallery -Pose OneD
Tools/lua/lua.exe Tools/GamePoseProbe/verify_animator_clock.lua
```

原始fixture在 `Saved/Tests/Animator292`；所有本輪RHI／UI使用專用 `Saved/DebugShots/Animator292_*` 路徑。stage286 T-pose根目錄與stage289–291歷史證據保留。錯誤與failed logs見 `Docs/ERROR_LOG.md` E-148。

## 範圍

此輪處理 source serialized speed multiplier 的正向播放與preview參數setter；不宣稱 automatic transitions／CrossFade、多active weighted layers／additive、Directional2D、mirror、IK、synced motions、完整HAnime layer／physics-enable初始化或shader pixel等價。下巴依使用者要求延期。

## 最終驗收（2026-09-29）

Stage292 `AnimatorClock292Verified.log` 全部PASS。五卡180個native raw frames、48,180 animated TRS attributes，90個非default raw positive frames；position max1.1920929e-7m、rotation components max1.7881393e-7、scale max2.3841858e-7，容差2e-5不變，159.53秒。source的MotionSpeed與speedHand都由真正經過時間控制驗證；後者是非1的source default=.5，不能當所有倍率default=1。

完整Session五卡180 frames，107,820全部body transform比較、3,905,676 runtime附件vertices比較，最大分量誤差0；900次invalid clock保持參數／時間並經後續physics／附件一致性控制，106.83秒。外部入口是傳入已native驗證的raw locals，再經相同helpers／ABMX／body與衣物物理，沒有在正式流程停用物理。

最後UE16distinct tests全Success／QueueEmpty16／exit0；其中clock三state共180frames、15個固定elapsed positive／restore控制；另1D450、Cartesian300 frames、15個blend參數控制、來源與外部乳搖各900負對照、sparse／invalid、mod catalogue、手勢與face／eyes／gaze回歸全部通過。初版production build26.41秒，最終含default-clock API相容及source-name gate的build24.73秒Success；所有最終UE／RHI／UI都用最終build。首輪nested-row-count failed queue完整保留在Saved/Logs/old/Animator292BeforeLayerCountFix。

舊native1D300frames／81,000 attributes與Cartesian720frames／194,400 attributes分別9.31／49.23秒PASS，沒有覆寫289／290原生fixture。原single-clip default sampler及override identity gate1passed，legacy Maker sampler5passed。Probe新regular-frame schema另用March重跑24筆，controller/baseState不為null；completion錯誤空，來源compiler與runner logs皆保留。

Gallery blend與clock各642個actual world display bones變化。速度slider倍率1.6，duration=.625倍、normalized phase保持、face clock保持、立即pose保持；再走.23秒才變化。1440×1000 UI已目視，圖Saved/DebugShots/Animator292UI_OneD。五卡RHI15圖900×1080與ContactSheet已目視；71attachment audits=0cm、15camera fit≤.92、五個眼高光與五個gaze display audits通過，未見新增側髮／髮飾／頸飾偏移。下巴延期，沒有新增shader pixel等價聲明。

正式圖Saved/DebugShots/Animator292_OneD_open；總覽Saved/Tests/Animator292/ContactSheet.png。Animator292Audit.json保存source／fixture／log／image SHA256與每項gate。E-148記錄所有本輪失敗與修正，舊根目錄T-pose286參考保持。完成的是通用正向source state clock；完整HAnime初始化、automatic transitions／CrossFade、多weighted layers及其他範圍仍需分別實作驗證。
