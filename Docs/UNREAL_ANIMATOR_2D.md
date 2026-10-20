# Unreal 來源 Cartesian BlendTree 與遮罩層（stage290）

Stage291 更正：本文件的 raw 原生 weights／duration／TRS 比對仍有效；原先 UI getter／finite pose 未證明逐幀入口使用指定參數。漏傳參數的根因與完整播放驗證見 [UNREAL_ANIMATOR_RUNTIME.md](UNREAL_ANIMATOR_RUNTIME.md)。舊截圖保留作歷史紀錄，指定參數的最新截圖使用 Animator291 路徑。

## 本輪實作

延續 stage289 的來源 AnimationClip／controller PPtr、1D、巢狀樹、卡片 baseline 與共用物理流程，新增 `m_BlendType=3` Freeform Cartesian 2D。參數、節點位置、pair vectors、inverse magnitudes 與 neighbor lists 全部從真正 bundle 解析；沒有角色名修正表。

四個 Studio controller 的 88 個 2D states／168 個 2D branches 位於額外動畫層。只混合子 clip 會把常數曲線寫到遮罩外骨骼，因此本輪同時實作明確選取一個 Override 層：基底使用該 controller 的 authored default state，兩層以相同 elapsed seconds 取樣，選取層 weight=1，其餘額外層 weight=0。

這個 API 是 explicit state playback。來源預設啟用的額外層、additive／synced motion sets／IK／mirror 等未支援情形仍拒絕；不宣稱完整 Animator state machine 或 Studio 所有多層互動流程完成。

## 混合公式與載入

來源 `m_ChildPairVectorArray[i*n+j] = P_i-P_j`，`m_ChildPairAvgMagInvArray` 的有效 pair 是距離平方倒數，`m_ChildNeighborListArray` 決定每個節點需檢查的 neighbor。

對輸入二維參數 p：

```text
raw_i = max(0, min(1, min_j[1 + dot(p-P_i, pair_ij) * inverse_ij]))
weight_i = raw_i / sum(raw)
```

只使用來源 neighbor pair；inverse array 的 self-pair diagonal Infinity 是合法、未使用的常數，不能整個 array 以非有限值一律拒絕。實際使用的 pair、維度、neighbor index／重複、parameter default 都要先驗證。支持範圍外輸入的來源權重，不硬裁成 bilinear grid。

1D 與 Cartesian 可遞迴組合，leaf 權重乘上父權重，動畫有效長度與 normalized phase 沿 stage289 通用處理；local position／scale 加權，quaternion 做 hemisphere 對齊及正規化。

Unity 官方將 type3 定義為 [FreeformCartesian2D](https://docs.unity3d.com/cn/6000.0/ScriptReference/Animations.BlendTreeType.FreeformCartesian2D.html)。上方具體公式由本機 serialized 常數推導，再以實際 Unity Animator 的 clip weights／length／local TRS 驗證；不是從官方類型名稱便推定數值等價。

## 遮罩與卡片通解

- `m_LayerArray` element 外層含 `data`，必須 unwrap 後讀 `m_SkeletonMask`。缺失的遮罩明確拒絕，不能當成合法空遮罩。
- 遮罩以 Animator 相對完整路徑的 Unity CRC32 hash 對應，從卡片實際最大 body `cf_j_root` tree 建立路徑；未知子路徑繼承父權重。body tree 外保留基底姿勢，不能按左右骨名猜遮罩。
- weight=0 精確複製基底 TRS，避免再次 normalize 已 unit 的來源 quaternion；weight=1 使用選取層；中間權重以 position／scale lerp 與 hemisphere quaternion blend。這次來源控制的 mask 是 0／1，中間 mask 權重未單獨取得 Unity 控制證據。
- 基底 state label 在 catalogue 去重後，以 serialized file／controller PathID／state index 身分解析；不使用可能加 suffix 前的同名字串。
- 在來源 Animator pose 後，繼續沿 ShapeBody.UpdateAlways、hands、scene helpers、ABMX、Ver02、衣物、頭髮與附件共用流程。沒有繞過已驗收的乳搖與 GPU follower palette。

## Unreal 操作

Gallery 選「控制器狀態：studio/anime/00」，載入來源後選：

```text
khs3_f_base3 / 17:Te_siri_D_momu_L.Te_siri_D_momu_L
```

顯示來源 `height`、`siriL_X`、`siriL_Y` 三個 float 滑桿。巢狀 `05:Te_mune_D_momu_L.Te_mune_D_momu_L` 則顯示 `Breast`、`height`、`muneR_X`、`muneR_Y`。參數更新保留 normalized phase，dt=0 不推進表情時鐘與物理歷史；Blueprint 沿既有 `SetGameAnimationFloat`。

診斷 `OK_SHOT_ANIM_FLOAT` 支持以分號分隔多個 `Name=Value`，每個都驗證與記錄，不只套第一個參數。這是截圖工具設定，來源參數 API 仍為通用入口。

## 驗收與產物

原生五卡每張 144 frames：兩個真實狀態（pure 2D、1D→2D）、18 個位置（9 source nodes、5 interior、4 outside）、4 phases。原生 Animator 停自動 tick，speed=1，明確 Play／Update(0)，只啟用選取 Override 層。base height=.37，nested Breast=.61；基底依相同 elapsed seconds 推算自己的 phase。

完整验收結果由 `Saved/Logs/Animator290Audit.json` 與 `Animator290Verified.log` 保存。驗收工具同時要求：

- 原生 leaf names／weights／duration、基底與選取層 animated local TRS 對照；原 tolerance=2e-5。
- stage289 五卡 1D／巢狀 1D 的 300 原生 frames 回歸，以及 controller／catalogue tests。
- UE 14 個 distinct tests，包括新 300 layered frames、原 450 1D frames、source 與 external body dynamics 各 900 負對照 frames、sparse／invalid 外部 clip、臉部與眼睛回歸。
- 真 RHI 五卡 15 張全身／臉／側臉圖，實際 output path、全部附件 audit、camera fits 與背景。
- 真 Gallery source／state／兩滑桿共四個 callbacks，phase／face clock 保持。

2026-09-29 最終全部 PASS：五卡 native720 frames／194,400 animated TRS **屬性樣本**，最大 position分量誤差7.450581×10⁻⁹ m、quaternion分量1.1920929×10⁻⁷、scale分量1.7881393×10⁻⁷。每卡原生去重cf transforms450–496，沒有把屬性樣本冒充全部骨骼或全部scalar分量。

release 原生2D gate161.34秒、既有1D五卡300 frames／81,000樣本回歸9.17秒，controller2／catalogue15項通過。UE14項全部Success／QueueEmpty14／exit0；初版build29.44秒、camera build12.51秒成功。最終RHI15圖逐張目視、71附件誤差0cm／15fits≤.92／背景一致；UI四個callbacks／三參數列／phase／clock通過。Audit保存五份native traces、controller audit、source／log／image SHA256。

原生 Animator global speed=1 不代表 state multiplier=1：layer17為1，nested layer5由 `m_SpeedParamID` 的 source default取.5，effective length相應加倍。驗收工具從serialized state／default float核對，沒有固定假設或放寬TRS tolerance。debug／CPU未完成抽取保留為partial logs，不計入上述正式release結果。

最新圖：`Saved/DebugShots/Animator290_Layer_open`；UI：`Saved/DebugShots/Animator290UI/gallery_imported_studio.png`；總覽：`Saved/Tests/Animator290/ContactSheet.png`。圖中是 base Idle＋單個額外層，不能稱為一般走路。

首輪目視發現躺姿低於固定站姿地板，頭部遭背景遮擋，而 portrait 鏡頭未跟隨頭部旋轉。共用拍攝改跟實際 head orientation，camera fit 保留 roll、沿 camera up 篩選上方 geometry；背景地板向下移至實際顯示 bounds 下方，不改角色、骨骼或物理。Gallery 的 `FrameCurrentPose` 同樣修正地板。動畫14項測試使用第一個 build；camera 修正另 build 後重跑 RHI／UI，數值公式未變。首輪15PNG及原logs／UI在各目錄 `old/BeforePoseCamera` 保留。

## 重現

PowerShell 在專案根目錄，所有 log 使用 absolute path：

```powershell
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/build.ps1'
& 'C:/portable/OpenKoikatsu/Tools/lua/lua.exe' 'C:/portable/OpenKoikatsu/Tools/anim_export/inspect_2d_graph.lua'
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator_2d_native.ps1' -Cards All
```

正式 Rust gate 使用與 UE 相同的 GPU 載入旗標及 release 編譯設定；runner 明確設定並保存每項 absolute log，assertions 與 2e-5 tolerance 不變：

```powershell
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator_2d_rust.ps1'
```

正常編譯專案後：

```powershell
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator_2d.ps1' -Mode Tests
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator_2d.ps1' -Mode Shots -Cards All -Blink open
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator_2d.ps1' -Mode Gallery
C:/portable/OpenKoikatsu/Tools/lua/lua.exe 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/verify_animator_2d.lua'
```

原生 runner 在 probe 已有 owner 時拒絕替換，按 owned exact executable／PID 清理，完成後移回 Tools；控制 JSON／37MB graph audit 留 Saved。重拍前封存舊確切15PNG及對應log／audit，不能混用不同 build 證據。

## 尚未涵蓋

Automatic transitions／CrossFade、Directional 2D type1／2、同時多個 active weighted layers、additive、同步層 motions、foot IK、mirror、完整 HAnime breast／layer 初始化、任意第三方 physics override／跨角色碰撞仍未完成。這次只驗證上述 source states，沒有宣稱所有 88 states 都已驗收。下巴輪廓線依使用者要求暫不處理。

本輪錯誤、根因推定修正與失敗 log 見 `Docs/ERROR_LOG.md` E-146。
