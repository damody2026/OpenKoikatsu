# Stage301：來源 Animator.CrossFade

## 來源與範圍

`kks_decomp/Managed/CharaStudio_Data/Assembly-CSharp/ChaControl.cs` 的
`setAnimPtnCrossFade`（817行）與 `Illusion.Game.Elements.EasyLoader/Motion.cs`
（95行）直接呼叫
`Animator.CrossFade`。NodeCanvas 的 MecanimPlayAnimation 把秒數除以來源
state.length 後呼叫它。這裡 duration 因而是來源 state 的 normalized 時間。

本輪實作同一來源 bundle、同一 controller 的單一 base layer，接受明確指定
的來源／目標 state，不依卡名決定載入方法。原有 motion tree、speed、cycle、
loop/clamp 仍由共用 sampler 處理；沒有另寫角色專用 offset。

## 時鐘與混合

- transition 秒數 = normalized duration × 當前來源 state 的有效 length。
- 來源 state 繼續播放；目標 normalized time = offset + elapsedDelta/toLength。
- transition progress 線性；位移／縮放線性混合，旋轉 shortest normalized lerp。
- 完成後保留目標 epoch／phase，不把 global elapsed 或表情時鐘歸零。
- duration=0 立即切換至目標 offset；NaN、Infinity、負 duration 或乘積 overflow
  原子拒絕。極大 finite duration 在短 state 上仍可能有效。
- 完成後修改 speed 時以當前 normalized phase 重設 epoch，保持 global elapsed。

新 C exports：`ok_animation_crossfade`、`ok_animation_elapsed`、
`ok_animation_in_transition`；它們不放入既有 `OkRustBindings`，保持 caller
buffer 大小。UE `FOkSession` bridge 與 Blueprint `CrossFadeGameAnimation`／
`IsGameAnimationTransitioning` 使用同一 Session 時鐘。runtime clocks 不序列化，
pipeline141／decoded90 未改。

## 驗證方式

五張卡的 native Animator 控制使用 `action/animator/00.unity3d / base`：
Idle→Locomotion_Anger、反方向 speed1.6，以及零 duration，含起點／混合中／
完成後。75 frames，34995 個完整 native transforms，由独立 Python 混合
驗證；Slerp／SmoothStep 作負向控制。

Rust raw 比對每個 sample 的有效 body Animator Transform 通道。serialized
zero scale 是共用 sampler 不寫入的通道，不能當作動畫值與卡片修形比較；
native probe 手動 Animator.Update 不再執行 LateUpdate，不能在 raw 測試額外
套 ABMX。Session→external pose 測試另驗 helpers、ABMX、物理與 private 附件；
同算法 Session 對照不冒充獨立原生證明。

重現：`run_crossfade_native.ps1` → `verify_native_crossfade.lua` → release
`crossfade_tests` ignored tests → UE production build → `run_crossfade.ps1
-Mode Tests` → `-Mode Tests -RegressionOnly` → `-Mode Shots` →
`verify_crossfade.py`。結果與 SHA 保存在 `Saved/Logs/AnimatorCrossFade301Audit.json`。
截圖目錄 `Saved/DebugShots/AnimatorCrossFade301_BaseCrossFade_open`，root Stage286
T-pose 截圖保留。post-capture 真正 Actor.Tick 驗 transition 完成與 follower palette。

## 尚未涵蓋

active transition 的 interruption／parameter mutation／trigger、多層／同步層
CrossFade、automatic FSM、pending triggers、完整 HandCtrl/item scheduler 尚未
實作，明確拒絕，不回退到不同 controller 或靜態 pose。全 mods／shader pixel
equivalence、Directional2D／mirror／IK／Timing=true／零負 speed 仍待；下巴輪廓
依使用者指令延期。本輪功能與證據不能當作整個專案完成。

錯誤與修正：Docs/ERROR_LOG.md E-157。正式數值與圖像驗收見 HANDOFF2.md §158。

## 已完成的數值驗證

| 驗證 | 數量 | 最大誤差／結果 |
|---|---:|---|
| 獨立 native 完整 Transform 混合 | 75 frames／34995 transforms | p7.2e-8、q1.2812e-7、s1.0e-7 |
| native 與 Rust 有效 body 動畫通道 | 75 frames／20625 attrs | p1.1921e-7、q6.7251e-7、s1.1921e-7 |
| Session／external 的後處理骨架 | 45000 TRS | 1.1921e-7 |
| private 附件頂點 | 1627365 | 0m |
| Session 非法／active 操作拒絕 | 525 | 不改參數與 elapsed |

正式 Rust 兩項153.44秒 PASS，原門檻2e-5；Slerp反例0.010781、SmoothStep
反例0.080302。Native state length 包含 speed：Idle speed1=6.761905s；
Anger speed1=1.46666694s，speed1.6=0.9166668s。首次 UE test header 編譯
失敗34.10秒，修正後 production build6.78秒 Succeeded；失敗 log 保留。

Unreal 新 CrossFade automation 五卡75frames PASS，published palette 共423骨
發生變化，285次非法／active操作拒絕；其測試不取代上表獨立原生數值比較。
九項既有回歸1795frames全部Success，包含1D450／2D300／additive180／
獨立clock100／motion time scale300／multi75／state clock180／trigger75／
Studio source135。新舊合計10distinct tests／1870sourceframes，QueueEmpty1/9，
兩個owned automation程序exit0。

RHI 五卡15PNG900×1080＋README，71附件 audit max0cm；15 framing fits≤.92；
PV 四個來源 slots 的424 follower samples max2.252e-6cm。全部 face/time/gaze/
highlight/EyeLook gates通過。post-capture Actor.Tick共15samples跨混合與完成，
follower palette error0cm；不宣稱還原物理粒子歷史。

完整15圖 contact sheet與March全身／正臉、Alice正臉、Yuuka正臉、KK091全身、
KK384正臉原圖已目視，未見本輪新增眉眼／側髮／髮飾／頸飾偏移。normal來源
与前輪H來源的姿勢／helper設定不同，不能把兩者截圖視為相同動畫的像素對照。
既有CommonUI viewport routing Error仍保留，沒有本輪HTTP listener bind errors；
Gallery C4701與cleanup warnings不冒稱零warning。

verify_crossfade.py完整PASS；root286 T-pose15圖SHA不變，probe不留game plugins，
五個native與三個UE owned程序均退出。正式數值、source/decomp／code／binary／
logs／15PNG hashes見Audit.json；歷史失敗logs另列，未冒充最終production結果。


## Stage302 後續

以上 Stage301 範圍與 audit 為當時證據。Stage302 新增同一 multi-layer graph
中的單一 independent child CrossFade，詳見 UNREAL_ANIMATOR_LAYER_CROSSFADE.md。
Concurrent／synced／active interruption 與完整 FSM 仍未完成。
