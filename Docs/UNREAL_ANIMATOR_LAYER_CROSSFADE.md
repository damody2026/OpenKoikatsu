# Unreal 獨立動畫層 CrossFade（Stage302）

## 來源與混合順序

來源 `ChaControl.setAnimPtnCrossFade`、`Illusion.Game.Elements.EasyLoader/Motion.cs`
及 `NodeCanvas.Tasks.Actions/MecanimPlayAnimation.cs` 呼叫 Animator.CrossFade。
normalized duration 乘來源 state 的有效 length，不能乘 base layer 的 length。
原生控制使用 `h/anim/female/05_01_00.unity3d / khr_f_02_00 / Base Layer.OLoop`。

1. base 與其它選定層持續使用各自 epoch、phase、speed、loop／clamp。
2. 選定獨立層的 from／to motion 同時前進；target epoch 是目前 global elapsed，
   target phase 是 normalized offset。progress 是線性 delta／transition seconds。
3. 混合該層的 raw pose（override）或 source reference delta（additive）：
   position／scale 線性混合，rotation shortest normalized lerp。
4. 再套來源 relative-path mask、runtime layer weight 與來源 additive／override
   公式，按 layer index 合成。不能混合兩個已完成多層合成的最終骨架。
5. 完成後保留 target epoch／phase；不把全域 elapsed、base、其它 layer 歸零。

共用 sampler 拆成 `sample_motion_pose`／`compose_motion_layer`；原有一般層與新
CrossFade 共用同一個 mask、weight、reference 及合成公式，沒有角色卡補丁。

## API 與相容檢查

Blueprint `CrossFadeGameAnimationLayer(Name, Layer, NormalizedDuration,
NormalizedOffset)`；Name 是完整 `states:layers@HEX(... )|baseLabel` 目標圖。
from／to 必須同 controller source（包括 scene profile／override identity）、
同 base state、相同 selected layer indices；只能改指定 independent child state。
該 child 必須同 controller／layer／base／mask／blend mode，不能是同步層。

全部 motion、speed、duration、offset、clock 在提交前驗證。拒絕非有限值、負
duration、乘積 overflow／underflow、future source epoch、未載入 layer、無效
目標或更動其它 layer。拒絕保持參數、state、elapsed；零 duration 立即提交
target clock。active transition 期間 interruption／float mutation／trigger 明確拒絕。

新增 C exports `ok_animation_crossfade_layer` 與唯讀
`ok_animation_transition_source_length`，均在既有 OkRustBindings 表外，沒有擴大
舊 caller buffer。pipeline141／decoded90 不變。Session 的 AnimDuration 在 child
轉場期間仍是 base length；唯讀來源長度查詢供實際 Actor.Tick 使用。

## 驗證

五張卡各 20 frames：獨立層 7／8／70 self CrossFade 到另一 phase，含選定層
weight=0、其它 fractional additive 層與同步 override 73，取樣起點、混合中、
完成後。原生該 controller 的 tested child machine 僅一個 state，因此原生證据
證明 self transition phase 混合；不宣稱已原生驗證所有不同 state 配對。

原生 probe 保存 combined、base、各 isolated layer、isolated from／to 與完整
final from／to。Python 由原 bundle 讀 masks／modes，獨立重建混合與合成。
錯誤 final-pose interpolation 作反例。Rust 另比對全部有效 body Transform
binding；serialized zero scale 不寫入、manual Animator.Update 不追加 ABMX。

Session／external pose 驗全部後處理骨架與 private 附件，包含物理；這是同
sampler 整合測試，不能替代獨立 native proof。UE automation 驗公開 API、原子
拒絕、clock／weight 保留、finite palette、完成與零 duration。RHI 五卡全身／
正臉／側臉，截圖後真正 Actor.Tick 驗 transition 完成及顯示 follower palette。

重現：`Tools/GamePoseProbe/run_layer_fade_native.ps1` →
`Tools/anim_export/verify_native_layer_fade.lua` → release `layer_fade_tests`
ignored tests → UE production build → `run_layer_fade.ps1 -Mode Tests` →
`-Mode Tests -RegressionOnly` → `-Mode Shots` → `verify_layer_fade.py`。
數據 `Saved/Tests/AnimatorLayerFade302`；截圖
`Saved/DebugShots/AnimatorLayerFade302_Layers7_8_73_open`；正式 audit 在
`Saved/Logs/AnimatorLayerFade302Audit.json`。

## 剩餘範圍

本輪是同時只有一個 child layer 的明確 CrossFade。並行 transitions、active
interruption、同步層 CrossFade、base transition 與 multi layers 同時運作、automatic
FSM／pending trigger／完整 HandCtrl item scheduler 仍未完成。不同 state 配對
的普遍原生證據、Directional 2D／mirror／IK／Timing=true／零負 speed、全 mods
及 shader pixel equivalence 亦未驗收。下巴輪廓依使用者指示延期。

錯誤與修正：Docs/ERROR_LOG.md E-158；驗收結果：HANDOFF2.md §159。


## 已完成的來源與 Rust 比對

| 驗證 | 數量 | 最大誤差／結果 |
|---|---:|---|
| 獨立 native 最終 Transform 合成 | 100 frames／46660 transforms | p1.49e-7、q2.46e-7、s6.0e-7 |
| native isolated from／to 混合 | 100 frames | p2.384e-7、q3.253e-7、s8.3e-7 |
| native／Rust 有效 body 動畫通道 | 27000 attrs | p1.1921e-7、q2.7381e-7、s1.1921e-7 |
| Session／external 後處理骨架 | 60000 TRS | 5.9605e-8 |
| private 附件頂點 | 2169820 | 0m |
| 原子拒絕／zero duration | 1100／5 | elapsed／params 保持；立即提交 |

錯誤 final-pose interpolation 的最大 q 差3.2912e-5，超出既有2e-5門檻；
正式兩項 Rust169.48秒 PASS。三月七初步單卡 log 與先前 compile failures
另列歷史，不當作五卡完成證據。初版 UE build35.96秒、修正spec後正式build8.32秒 Succeeded，仍
保留既有 Gallery C4701 warning；不宣稱專案零warning。


## Unreal 公開 API 驗證

新 automation `OpenKoikatsu.AnimationCatalog.AnimatorLayerCrossFade` 已通過五卡
100frames、1100次拒絕、5次零duration，公開palette共有5骨改變。可見控制
採第8層（cf_j_waist02）；第7層有效raw變化不保證所有卡公開skinned palette
都改變，不能為了讓測試變色而補償mesh或提高動畫權重。首次失敗、修正前
partial regression及build均保留歷史；完整regression與RHI須採正式binary。


正式十項回歸1870sourceframes全部Success（QueueEmpty10、exit0）：1D450、
2D300、additive180、base CrossFade75、independent clock100、motion time scale300、
multi75、state clock180、trigger75、Studio source135。連同新功能100frames，
共11distinct tests／1970sourceframes；使用最後正式build的同一執行檔。


## 最終 RHI 與保存

五卡15PNG900×1080＋README；71附件 audit max0cm、15framing fits≤.92，
PV四slots共424follower samples max2.252e-6cm。Face／time／gaze／highlight／
EyeLook gates全部通過。截圖後真正Actor.Tick共15samples跨轉場與完成，
其它clock／weight保持、follower palette error0cm。沒有宣稱還原particle history。

完整15圖contact sheet與March全身／正臉、Alice正臉／側臉、Yuuka正臉、
KK091全身、KK384正臉／側臉原圖已目視，未見本輪新增眉眼／側髮／髮飾／
頸飾偏移。KK091胸前彎曲前髮為Stage298已有；source palette proof不等於
全部hair dynamics或遊戲shader像素認證。下巴輪廓仍延期。

verify_layer_fade.py完整PASS，root Stage286 T-pose15圖SHA不變，新資料夾只有
本輪15PNG＋README。probe不留game plugins，十個owned native／UE程序均退出，
包括兩個失敗／partial程序。原CommonUI routing Error及Gallery warning／
cleanup hitches、HTTP announcement timeout保留；無本輪listener bind errors。
source/decomp、code、binary、logs、fixtures及PNG SHA在Audit.json保存，歷史
failures與partial logs另外列出，不冒充正式五卡／最後binary驗證。

## Stage303 接續

不同獨立 child 的並行 CrossFade 接續實作於
UNREAL_ANIMATOR_PARALLEL_CROSSFADE.md。上述單一轉場驗證與歷史結果保留；
目前允許其它 child 正在轉場時開始新的 child，同一 child active interruption
仍拒絕。查詢各自source length／狀態應使用新增逐層API。
