# Unreal 來源 trigger self re-entry

## Stage300：來源規則與通用載入

`HandCtrl.AnimatrotRestrart`（反編譯 HandCtrl.cs 2789 附近）對女性 Animator 與互動 item 設定 `Restart` trigger，沒有把全域時間歸零。來源 `h/anim/female/05_01_00.unity3d / khr_f_02_00` 的獨立層7/8/70有 AnyState → current state 的瞬時 transition；base與同步73/74沒有該規則。因此逐層重新起播，主動畫／同步層持續前進。

共用 controller loader 從 `m_Values.m_ValueArray` 的 type9辨識 trigger，名稱從來源 TOS取得； production parser／evaluator沒有把 `Restart` 或角色卡名寫成判斷條件。測試使用 Restart 只是選擇真實來源 fixture，unit另用 Pulse確認來源驅動。

| 來源欄位 | 本 API 接受條件 |
|---|---|
| m_ConditionConstantArray | 唯一正向 trigger condition，mode1 |
| m_DestinationState | 目前選定 state index |
| m_TransitionDuration | 0 |
| m_HasExitTime | false |
| m_CanTransitionToSelf | AnyState rule為true |
| m_TransitionOffset | finite，作為新的 normalized start phase |

先檢查 AnyState，再檢查 current-state transitions；第一個同名但不支援的規則保留為 blocker，整次操作拒絕，不能跳過它只重啟部分層。Base有同名rule、同步層有同名rule也拒絕。這個明確選定層 API 不模擬整份 controller 的自動狀態排程。

## API 與時鐘

- C exports：`ok_animation_trigger_count`、`ok_animation_trigger_at`、`ok_set_animation_trigger`。三個獨立symbol沒有擴大 `OkRustBindings` 舊table的配置或寫入大小；header／Rust staticlib／UE plugin一起編譯。
- FOkSession：`AnimationTriggers()`、`SetAnimationTrigger(Name)`。
- Blueprint／PreviewActor：`GetGameAnimationTriggers()`、`SetGameAnimationTrigger(Name)`。
- 觸發時用目前 Session elapsed 作為每個受影響獨立層的 `OK.LayerStartTime.N`，phase取來源offset。權重0的層也重新起播，符合原生 Animator。
- 以 candidate parameters 一次驗證並提交；拒絕後所有 parameters／global elapsed保持。未起播的future epoch拒絕，避免悄悄提前啟用層。
- 不修改base duration、其它weights／float、未受影響層、face clock或physics history。Session只改clock controls；Actor隨即在相同elapsed以dt0發布新palette。
- Unreal game playback保存unwrapped elapsed，由每個source state自行loop／clamp。Shot timeline也走共用 ApplyPoseFrame；不再用base duration截短取樣。

這是即時 self re-entry API；呼叫後立即處理當前elapsed，沒有Unity pending trigger佇列。Base transitions、cross-state、crossfade、exit-time／多conditions、全 HandCtrl／item scheduler、automatic transitions仍需完整state machine；沒有將其宣稱已完成。零／負speed、Directional2D、mirror／IK、Timing=true、完整Studio scene及所有mod／shader像素認證也仍待。下巴輪廓依使用者指令延期。

## 原生對照與驗證方法

五張卡共三組layer選擇：7/8（.63/.37）、8/70（.37/.63）、7/8/73/74（0/.37/.41/.59）。起始clock沿Stage299：7 epoch-.23／phase.2，8 epoch.11／phase.37，70 epoch-.29／phase.61。原生 Animator 在 .61與1.07秒呼叫 SetTrigger＋Update(0)，於 .49／.61／.73／1.07／1.7秒取樣，共75frames。

每frame保存native合成、base、每層isolated weight1及錯誤reset-base的負向控制。數值fixture關閉原生DynamicBone；另以physics啟用的Rust Session和UE驗證其整合。UnityPy依source masks／layer modes獨立重建native transforms，不借用Rust合成器；逆序及reset-base負向控制必須超過2e-5門檻。

RHI使用同來源7/8/73，elapsed1.19，兩次source events，嘴8/.8、blink open、gaze(.6,-.4,-.4)、EyeLook1。每卡三張capture之後才跑3次真實Actor.Tick＋trigger，確認global／face clock、weights保持且發布palette改變；恢復parameters／elapsed後載下一卡。沒有宣稱恢復particle／face history。

## 重現

```powershell
# C:/portable/OpenKoikatsu
& Tools/GamePoseProbe/build.ps1
& Tools/GamePoseProbe/run_restart_native.ps1 -Cards All
Tools\lua\lua.exe Tools/anim_export/verify_native_restart.lua
# C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib trigger_tests -- --ignored --nocapture
cargo test --release --lib trigger_metadata_keeps_priority_and_unsupported_guards
# build Unreal後，C:/portable/OpenKoikatsu；UE各步依序執行
& Tools/GamePoseProbe/run_restart.ps1 -Mode Tests -RestartOnly
& Tools/GamePoseProbe/run_restart.ps1 -Mode Tests -RegressionOnly
& Tools/GamePoseProbe/run_restart.ps1 -Mode Shots -Blink open -Cards All
Tools/lua/lua.exe Tools/GamePoseProbe/verify_restart.lua
```

## 驗收結果

完整 native composition：75frames／34995 Transform snapshots，max position6.15e-9／quaternion1.08783e-7／scale3.33822e-8；逆序誤差.000319691與錯誤reset-base誤差.180405699超過原門檻。

正式Rust兩項205.62秒PASS：75frames／20250來源CF骨TRS attrs，max position1.1920929e-7／rotation1.7881393e-7／scale1.1920929e-7；同門檻2e-5。Session75frames／45000完整骨TRS／1627365 private附件頂點，max骨5.9604645e-8／附件0m，physics啟用；25 malformed descriptors、倒序選擇、無效trigger／FFI buffers及future epoch atomic拒絕均通過。來源rule優先序／unsupported／非trigger type的unit PASS；三月七單卡首輪與partial composition留作歷史，正式採完整五卡log。

Production Rust staticlib／UE plugin build成功（首輪31.11秒因test變數遮蔽失敗，修正後6.91秒完成link）。Unreal新test五卡75frames、90次fixture trigger、225次非法trigger拒絕；五卡published palette各1骨改變，QueueEmpty1／exit0。

八项既有回歸全部Success：獨立clock100／multi75／Additive180／1D450／2D300／state clock180／Studio135／motion scale300，共1720frames；連同新test合計9distinct／1795sourceframes，QueueEmpty1／8、各exit0。

RHI完整五卡15PNG900×1080、71attachments max0cm、15fits≤.92、424followersamples／palette最大誤差2.252e-6cm，五卡face time／gaze／eye highlight／EyeLook／Auto scene gates全部PASS。Capture timeline兩次events/card，共10次；每卡截圖後Actor.Tick三次與即時trigger共15samples／15events，保持global／face時計與weights，發布骨骼每event1骨變化、每卡3筆。這些是不同測試計數，不能把90fixture events、10capture events和15Tick events混用。

已目視全部15圖總覽及March全身／正臉、Alice正臉、KK091全身、KK384正臉原圖；未見本輪新增眉眼／側髮／髮飾／頸飾偏移。KK091胸前彎曲前髮在Stage298已有，不當新trigger的頸飾退步，也不推稱所有hair physics已達遊戲像素等價。PV display驗證palette／followers傳遞，source H沒有啟用Studio PV。

`verify_restart.py`完整PASS，`Saved/Logs/AnimatorRestart300Audit.json`／Verified.log保存來源／native／code／binary／logs／captures SHA256。新圖僅在`Saved/DebugShots/AnimatorRestart300_Layers7_8_73_open`的15PNG＋README，ContactSheet在`Saved/Tests/AnimatorRestart300`。root Stage286 T-pose15圖SHA不變、probe不留game plugins、所有本輪owned native／UE均退出。pipeline141／decoded90維持，無角色卡特例。

RHI log仍有既有CommonUI viewport routing Error；無本輪HTTP listener bind錯誤。初次build保留舊Gallery C4701 warning，cleanup hitches如實保留；不能將動畫gates通過概括為全專案零error／所有mod／shader認證。上列即時self re-entry範圍完成，完整state machine依剩餘範圍繼續追蹤。
