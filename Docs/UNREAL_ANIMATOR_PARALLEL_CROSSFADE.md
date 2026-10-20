# Unreal 並行獨立動畫層 CrossFade（Stage303）

## 來源與通用處理

`ChaControl.setAnimPtnCrossFade`、`Illusion.Game.Elements.EasyLoader/Motion.cs`
及 `NodeCanvas.Tasks.Actions/MecanimPlayAnimation.cs` 呼叫 Animator.CrossFade。
EasyLoader 對指定 layers 逐層呼叫；不把全部層折成單一轉場時鐘。
來源 duration 為 normalized source state length，包含來源 motion speed。

Session 以 layer index 保存每個 child 的 from/to clock、起點與有效來源長度。
第二個 layer 開始時，第一個 layer 的進度不重設。每次取樣先取 base，再按
來源 layer index，混合該層的 raw pose／reference delta，套來源 path mask、
runtime weight 與 additive／override 合成公式。各自完成後移除該層的轉場，
保留 target epoch／phase，其它轉場繼續。不歸零 global elapsed 或 FaceClock。

這套共用 loader 不依卡名／髮型／身體比例判斷。weight=0 的層仍有來源
時鐘與轉場；zero duration 立即提交該層，不清除其它層。既有 shaping、
FBS、private attachment 与 body dynamics 仍在動畫結果之後處理。

## API 與界限

- Blueprint `CrossFadeGameAnimationLayer(Name, Layer, NormalizedDuration,
  NormalizedOffset)` 可在其它獨立 child 正在轉場時呼叫。
- `IsGameAnimationLayerTransitioning(Layer)` 與
  `GetGameAnimationLayerTransitionSourceLength(Layer)` 查指定層。
- C exports `ok_animation_layer_in_transition` 與
  `ok_animation_layer_transition_source_length` 在 OkRustBindings 外，舊 caller
  buffer 大小不變。Layer 0 查 base；負數／不存在／已完成返回 0。
- 舊 global in_transition 表示任一層 active；舊 source_length 在只有一層時
  保留原語義，多層並行返回 0。呼叫端應指定 layer 查詢，不能選第一／最後層。

目標必須與當前圖同 source/controller/profile/base、同 selected indices，
只更動指定獨立 child。每次完整驗證 candidate 才提交，保持其它層的參數與
state。非法／非有限值、future source epoch、未載入 index、同步層拒絕。
同一層 active interruption、base+multi 並行與 active float/trigger 仍明確拒絕。
SetAnimClip 重設兩組轉場與 clock；runtime 狀態不寫入 cache，pipeline141／
decoded90 不變。

## 原生控制與分層證據

五卡各 4 scenarios × 5 frames，來源
`h/anim/female/05_01_00.unity3d / khr_f_02_00 / Base Layer.OLoop`。
選定 7/8/73 或 8/70，weights .63/.37/.41，獨立 epoch/phase。
兩層不同起點與 duration，包含 zero-weight layer7、nonloop layer70、同步73，
以及相同時間按 8→7 註冊（合成仍依來源 index）。

Probe 導出 combined/base/isolated layers/每個 event 的 from/to、normalized clocks
和 transition flags。Python 由原 bundle 讀 masks/modes，獨立重建逐層混合。
錯誤 collapsed-progress 控制使用最後 event 的 alpha 混合所有層，會超門檻。
Rust 比對全量有效 body Transform bindings；serialized zero scales 保留卡片值，
manual Animator.Update 不追加 ABMX。

Session/external 對照驗完整後處理骨架與 private 附件（physics 啟用），是同
sampler 的整合證據。五卡額外 zero-duration-during-active 是 API 整合控制，
不冒稱本輪 100 native frames 亦包含 zero duration。tested child machines 只有
一個 state，原生證據是 self CrossFade 不同 phase；不泛稱所有 state 配對已驗證。

UE automation 驗 C bridge/逐層查詢/原子拒絕/獨立完成/finite published palette，
公開可見控制用 layer8。RHI 五卡15圖在 elapsed1.04、兩層皆 active 截取。
截圖後直接 Actor.Tick 到1.04/1.25/1.92，確認第一層完成而第二層繼續，
最後全完成、follower palette同步。不聲稱恢復已跑過的 particle history。

## 重現

1. `Tools/GamePoseProbe/run_parallel_fade_native.ps1 -Cards All`
2. `Tools\lua\lua.exe Tools/anim_export/verify_native_parallel_fade.lua`
3. 在 Tools/ok-extract 執行 `cargo test --release --lib parallel_fade_tests -- --ignored --nocapture --test-threads=1`
4. UE production build；`run_parallel_fade.ps1 -Mode Tests`
5. `-Mode Tests -RegressionOnly`，再 `-Mode Shots`（依序避免 listener bind 衝突）
6. `Tools/lua/lua.exe Tools/GamePoseProbe/verify_parallel_fade.lua`

資料 Saved/Tests/AnimatorParallelFade303；新圖
Saved/DebugShots/AnimatorParallelFade303_Layers7_8_73_open，15PNG+README。
Stage286 正式 T-pose SHA 保持。錯誤 Docs/ERROR_LOG.md E-159，接續 HANDOFF2.md §160。

## 尚未驗收的範圍

同層 interruption、同步層 CrossFade、base+multi transitions、automatic FSM／
pending triggers／完整 HandCtrl item scheduler，所有異 state 配對及未覆蓋 modes、
Directional2D/mirror/IK/Timing=true/零負speed、全 mods／shader pixel equivalence。
下巴輪廓依使用者指示延期。本輪並行轉場的完整 gate 結果如下；其餘範圍維持待辦。

## 已完成的原生與 Rust 比對

| 驗證 | 數量 | 最大誤差／結果 |
|---|---:|---|
| 獨立 native 最終 Transform 合成 | 100 frames／46660 transforms | p1.30e-7、q2.98e-7、s7.90e-7 |
| native isolated transition 混合 | 200 event snapshots | p2.77e-7、q4.325e-7、s8.40e-7 |
| native／Rust 有效動畫通道 | 27000 attrs | p5.9605e-8、q2.9802e-7、s1.1921e-7 |
| Session／external 後處理骨架 | 60000 TRS | 5.9605e-8 |
| private 附件頂點 | 2169820 | 0m |
| 非法操作原子拒絕 | 2600 | params/state/elapsed/其它fade保持 |
| zero duration during active | 5 | 另一zero-weight layer的fade不被清除 |

錯誤 collapsed-progress 反例差0.003501，超2e-5；所有正式比對維持原門檻。
Rust 正式兩項223.31秒PASS。RustDebugPartial與40frames native Partial另列歷史，
不當五卡正式證據。首輪 UE production build44.35秒Succeeded，補齊Actor來源floats後正式build13.60秒Succeeded，既有
BpGeneratorUltimate dependency與Gallery C4701 warnings保留。

## 驗證流程修正

`SetGameAnimation`／`SetAnimClip` 會清除 floats；post-capture audit 必須重新設定
height=.37、height1=.61、Breast1=.37，再用 native 的絕對完成時間判斷。
已在RHI前review修正；首輪成功new target與中斷partial regression保存
BeforeActorSourceParameters，正式gates採最後binary。不是卡片比例補償。

runner 在Wait前保留Process.Handle、結束後Refresh並拒絕null ExitCode；
避免空白exit誤當0或Shots跳過README。原先new target的UE RequestExitWithStatus
為0，該runner自身未取得process exit code；修正後正式new/regression/RHI重新驗證。

## 正式 Unreal 公開 API

Automation `OpenKoikatsu.AnimationCatalog.AnimatorParallelCrossFade` 在最後
production binary通過五卡100frames/200events、2600原子拒絕、5次zero duration
在另一層active時完成，published palette共5骨變化。逐層source length查詢、
第一層完成後第二層仍active、全完成與SetAnimClip reset均通過；QueueEmpty1、
UE RequestExitWithStatus0與修正runner取得exit0。

## 同版引擎回歸

十一項既有automation全部Success/QueueEmpty11/UE與runner exit0：1D450、2D300、
additive180、motion time/speed300、multilayer75、independent clocks100、state clock180、
Studio source135、trigger75、base CrossFade75、single child CrossFade100，合計1970
來源frames；加新並行100，共12distinct tests/2070來源frames。同一最後production
staticlib/plugin binary；首輪partial results不算回歸完成。

## RHI、目視與完整驗收

五卡15PNG900×1080+README，elapsed1.04、mouth8/.8、blink open、
gaze(.6,-.4,-.4)、EyeLook1、source Auto H。10個capture CrossFade events不重設
elapsed。71attachment audits最大0cm，15fits皆≤.92，424PV follower samples
最大2.252e-6cm；五卡face-time/gaze/highlight/EyeLook/scene gates全部通過。

截圖後5卡×3直接Actor.Tick（共15samples）驗各層完成先後、clock/weight保持、
base長度與完整follower palettes，最大0cm。對照sheet全15圖及8張重點原圖，
未見本輪新增眉眼／側髮／髮飾／頸飾偏移；KK091胸前彎曲前髮仍存在，
是Stage298已有問題，也對照Stage302原圖確認，未在本輪修正。不把這些geometry
與播放證據泛称所有hair physics或shader pixel equivalence通過。

CommonUI routing既有Error、BpGeneratorUltimate dependency、Gallery C4701與
cleanup/HTTP timeout warnings保留；完整流程無新HTTP listener bind error。
verify_parallel_fade.py完整PASS，Stage286正式15Tpose SHA不變，native probe
不留game plugins，本輪12個owned native/Rust/UE進程（含partial/中斷）均已退出。

完整source/decomp/binary/logs/native fixtures/15PNG hashes保存
Saved/Logs/AnimatorParallelFade303Audit.json；Verified.log正式PASS。
ContactSheet在Saved/Tests/AnimatorParallelFade303/ContactSheet.png。
