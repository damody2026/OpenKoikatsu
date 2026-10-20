# Unreal 多層獨立起播時鐘

後續Stage300已補來源trigger的瞬時selected-layer self re-entry，見`UNREAL_ANIMATOR_TRIGGER_REENTRY.md`。以下Stage299結果保留原驗收時間與範圍，完整FSM／HandCtrl scheduler仍待。

## Stage299 來源與範圍

Stage298支援多active layers，但所有層的elapsed從同一零點計算。此輪沿共用Animator取樣器補每層起播epoch與normalized初始進度，適用所有能載入的來源graph，不按卡名判斷。

來源查核：`ChaControl.setPlay`直接呼叫`Animator.Play(name,layer)`；`HandCtrl.AnimatrotRestrart`實際是對item及female設定`Restart` trigger，不是逐層Play。後者應由serialized transitions決定哪些state重新進入，本輪不把起播API當作完整HandCtrl trigger scheduler。

## 共用時鐘

既有`states:layers@HEX`描述與`OK.LayerWeight.N`不變。Non-synced選定層新增兩個runtime float：

| 控制 | 定義 | 預設 |
|---|---|---|
| OK.LayerStartTime.N | 相對整組elapsed零點的起播時間，秒；可負值表示已提早開始 | 0 |
| OK.LayerStartPhase.N | 起播時的normalized progress；允許有限的負值／超過1值，依來源loop／clamp處理 | 0 |

`phase = (elapsed-startTime)*sourceSpeed/effectiveDuration + startPhase + sourceCycle`。

- 起播時間之前，這個選定層不參與合成；到達epoch後保留其來源weight/mask/mode。
- 每層依自己的有效duration／state speed／cycle／loop取樣；sync Timing=false層不公開獨立clock，沿用base normalized clock。
- Base duration／loop不變，修改clock controls不重設其他層時間、global time或physics history；現有FFI／Unreal float API可用，無新ABI。
- Unreal game sampler播放保存未wrap elapsed，由各state loop／clamp，不能在Actor按base duration Fmod／Min；這樣epoch在主動畫一輪之後、non-loop base結束後的其它layer仍可播放。既有UE Sequence路徑不變；修改會改duration的來源blend參數仍保留既有normalized base phase換算。
- 所有controls要求finite。新時鐘以f64完成中間相位與wrap／clamp，避免合法finite f32起播值造成中間overflow；全零clock沿用原f32路徑，維持舊行為。既有f32::MAX末幀sentinel規則保留。
- Synthetic float IDs從不與任何來源float IDs重複的空位依canonical layer order分配，避免用固定arithmetic lane造成另一層control ID碰撞；來源reserved名稱碰撞拒絕。
- UI slider範圍只供預覽（time -10..10秒、phase0..1），不當作來源數值有效範圍。
- 持續沿用同一shaped baseline、Additive reference／來源path mask、FBS/gaze／附件followers／physics與GPU pipeline；下巴依使用者要求延期。

## 原生控制

來源 `h/anim/female/05_01_00.unity3d / khr_f_02_00`、base `Base Layer.OLoop`，選定Additive7/8/70與synced Override73/74。

沿Stage298五組layer/weight組合，每組elapsed .05/.17/.61/1.7，共20frames/card、五卡100。第0組7從0開始、8在.11秒起播；其餘7 epoch=-.23／phase=.2，8 epoch=.11／phase=.37，70 epoch=-.29／phase=.61。包含尚未起播、延遲起播、不同初始進度、不同source state speed、loop、非loop end-hold與同步層。

原生probe手動推進Animator到每個正epoch，呼叫Play(normalized phase)並啟用layer weight，舊層及base繼續前進。負epoch轉為初始normalized phase。保存native合成、base、各層isolated weight1、clock／clips，另存全部零clock的原生負向控制。不觸發Restart；此為明確Play能力的控制，不是HandCtrl交互scene。

UnityPy獨立讀來源mode／binary path masks，以原生base＋isolated snapshots重建，並驗證reverse order與shared zero clock會產生可量測誤差。binary masks控制不等於所有mod fractional mask驗收。

## 重現

```powershell
# C:/portable/OpenKoikatsu
& Tools/GamePoseProbe/build.ps1
& Tools/GamePoseProbe/run_layer_clock_native.ps1 -Cards All
Tools\lua\lua.exe Tools/anim_export/verify_native_layer_clock.lua
# C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib layer_clock -- --ignored --nocapture
cargo test --release --lib layer_clock_extremes
# build Unreal後，C:/portable/OpenKoikatsu
& Tools/GamePoseProbe/run_layer_clock.ps1 -Mode Tests -LayerClockOnly
& Tools/GamePoseProbe/run_layer_clock.ps1 -Mode Tests -RegressionOnly
& Tools/GamePoseProbe/run_layer_clock.ps1 -Mode Shots -Blink open -Cards All
Tools/lua/lua.exe Tools/GamePoseProbe/verify_layer_clock.lua
```

## 剩餘範圍

實際controller transitions／Restart／CrossFade、完整HandCtrl自動排程／item animation、獨立Override未公開的state組合、nonzero default extra layer自動初始化、Directional2D／mirror／IK、零／負speed、Timing=true、完整scene options與所有mod／shader pixel認證仍待各自處理。這裡的scalar clock是可重現取樣設定，不宣稱已完成觸發式動畫state machine。

## Unreal實際播放 gate

每卡三張RHI截圖完成後，在同一Actor以Tick推進8個samples（loop/end-hold各4），跨三個base週期，layer8 epoch設定base duration+.11秒，確認未wrap時鐘與published骨架finite，拒絕負delta不改時間。共五卡40samples。這裡loop/end-hold是UE driver的bGameAnimLooping true/false設定，來源base仍是OLoop；非loop來源末幀另由layer70原生數值控制涵蓋，不當另一個nonloop base的native驗收。gate在截圖後執行，避免改变已取樣的physics／face history；恢復parameters與elapsed後載入下一卡，不宣稱還原particle history。來源native／Rust數值驗證與此driver驗證分開記錄。

## 驗證結果

五卡正式Rust兩項236.46秒PASS：native100frames／27000 TRS attrs（每frame完整90來源CF骨×3），position／rotation／scale max為1.1920929e-7／1.7881393e-7／1.1920929e-7，2e-5門檻未降低。25個unsupported descriptors拒絕，選定layers倒序描述與source index正序結果逐值相等。

Session100frames／60000完整骨TRS／2169820 private附件頂點，max骨5.9604645e-8、附件0m，physics啟用。finite clock極值／legacy default phase unit PASS；非法clock與weight setter拒絕，全部嘗試之前的parameters／global time snapshot保持。

UnityPy獨立依來源mode／binary path masks重建46660個native Transform snapshots：max position2.6e-7／rotation1.19e-7／scale1.6e-7。逆序誤差.00030309094與共享零clock誤差.13079416，均超過來源門檻，正確排除錯誤合成。同步層73/74 clock仍沿base。

最終production Unreal build23.41秒Succeeded（先前52.72秒build與首輪UE/RHI保留BeforeActorClockGuard，不當最終actor clock證據）。八項distinct UE tests Success，1720sourceframes：new clock100＋multi75／additive180／1D450／2D300／state clock180／Studio135／motion scale300。QueueEmpty1／7、各exit0；新test五卡published palette各1骨隨start phase改變，共5，520次非法weight與1320次非法／sync clock拒絕。

RHI五卡15PNG900×1080，71附件audit max0cm、15fit≤.92、424followersamples／bone→palette max2.252e-6cm。五卡face time／gaze／eye highlight／EyeLook／source Auto H gates通過；四組PV display只檢查bindings／palette／followers，H context不啟用Studio PV。五卡實際Actor.Tick共40samples跨三個base週期與base duration之後的epoch通過；此gate在三張截圖之後，沒有污染capture物理歷史。

已目視完整15圖ContactSheet、March全身／正臉、Alice正臉、KK091全身、KK384正臉原圖，眉眼／側髮／髮飾／頸飾未見本輪新增幾何回歸。KK091胸前彎曲髮束在Stage298原圖已存在，不誤認新時鐘的頸飾退步；沒有宣稱該髮束已獨立完成遊戲物理像素對照。既有CommonUI routing Error／resource cleanup hitches與Gallery C4701警告仍如實保留；最終依序啟動後無本轮HTTP listener bind errors。

`verify_layer_clock.py`完整PASS；`Saved/Logs/AnimatorLayerClock299Audit.json`／Verified.log保存code／binary／native／logs／PNG hashes。`Saved/DebugShots/AnimatorLayerClock299_Layers7_8_73_open`只含15PNG＋README，上一版移到DebugShots/old/AnimatorLayerClock299BeforeActorClockGuard。root Stage286 T-pose15圖SHA256未變，native probe在Tools、owned native／UE進程均退出，未保存遊戲卡／scene／配置。整個Animator state machine／所有mod／shader pixel等價仍依剩餘範圍，不宣稱全專案已完成。
