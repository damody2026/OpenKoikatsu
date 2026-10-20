# GPU Skin 即時形變

> 2026-09-22。即時 demo 的規格。抽取對 dump 的路徑（`ok.ShotPreview`、`OK_NOPOSE` 未設時的 `apply_mannequin_pose`）不要改。
>
> 依據是遊戲反編譯，不是新算法：
> - `ChaControl.SetShapeBodyValue` / `LateUpdateForce` / `UpdateShapeBody`
> - `ShapeInfoBase.ChangeValue` + `ShapeBodyInfoFemale.Update` / `UpdateAlways`
> - `ShapeHeadInfoFemale.Update`（臉，在身體之後，吃 `cf_s_head` 世界縮放）
> - `FBSCtrlMouth.CalcBlend` + `FBSBase.CalculateBlendShape`（`SetBlendShapeWeight`）
> - 既有移植：`Tools/ok-extract/src/shape.rs`、`lib.rs::mouth_ptn_pair`、`skel_export.rs`
>
> C++ 熱路徑移植 `shape.rs`，不從 decomp 再推一套。Rust 版留作 oracle。

## 0. 目標

載入一張卡之後，Rust session 可以關掉。之後：

- 96 支形變滑桿（身體 44 + 臉 52）拖動時，同一幀只改骨頭 local。
- 嘴巴開合只改兩個 blendshape 權重，在 **GPU skin 之前** 加到頂點上。
- T-pose 與 Unreal 走路動畫只寫 `cf_j_*` 旋轉。
- 畫面上只有 GPU skin。CPU skin 是除錯對拍，預設關。

遊戲就是這樣快的：`SetShapeBodyValue` 只取樣 key、設 `updateShapeBody`；`LateUpdateForce` 一幀寫幾十個 `Transform`；頂點是 `SkinnedMeshRenderer` 的 GPU skin。

## 1. 不做

- 存卡、顏色滑桿、衣服 ID、DynamicBone 胸部物理、FinalIK。
- 把 UE 走路寫進 `apply_mannequin_pose`。走路是 demo 重定向，只在 C++。
- 滑桿路徑呼叫 `Session.Skin()` 或重寫 ProceduralMesh 頂點。
- 用 `OKAddMorphs` 現況直接掛 morph。它的 delta 是 mesh 局部空間，而且沒人呼叫。UE 預設 morph 常常先在 CPU 寫 vertex buffer，那不是熱路徑。
- 把走路旋轉乘進 `cf_s_*` / `cf_d_*`。形變骨和動畫骨是兩組，階層自己乘。

## 2. 遊戲對照（要抄的切分）

```
滑桿事件  SetShapeBodyValue(index, value)
          → shapeValueBody[index] = value
          → sibBody.ChangeValue(index)     // 只重取樣這一類的 anm key → dictSrc
          → updateShapeBody = true         // 同一幀多支滑桿只亮一次

LateUpdateForce
          → UpdateShapeBody()              // ShapeBodyInfoFemale.Update：寫 cf_s_* / cf_d_* / cf_n_*
          → UpdateShapeFace()              // 用 cf_s_head 世界縮放
          → UpdateAlwaysShapeBody()        // cf_d_shoulder_L/R 的 X
          → 清旗標

Animator   只寫 cf_j_* 旋轉（T-pose = rest，不播 Stand_D_00）
嘴巴       SetBlendShapeWeight(close, 100-open*100) + SetBlendShapeWeight(open, open*100)
           open>0 才顯示 cf_O_tooth / cf_O_canine / o_tang
GPU        SkinnedMeshRenderer：blendshape 然後 skin
```

`ShapeBodyInfoFemale.DstName` 沒有 `cf_j_*`。`Update()` 寫的是 `cf_n_height`、`cf_s_head`、`cf_s_bust*`、`cf_s_thigh*`、`cf_d_*`。Clip 寫 `cf_j_*`。不要把兩組合成到同一根骨上。

低模把滑桿壓進 0.2–0.8。Maker / 本 viewer 是 hiPoly，不套那個 remap（`shape.rs` 已是這條）。乳頭起立是 `Lerp(shape[13], 1, nipStandRate)`，不是第 97 支自創滑桿。

## 3. 架構

```
Rust 抽一次（session_open）
  骨架 rest、v_ref、權重、key 表、correct、ABM、嘴 delta
        │
        ▼
C++ 持有這包，之後不再呼叫 apply_shape / skin
        │
        ├─ FOkShapeSolver     dirty 才 Update()，寫形變骨
        ├─ FOkPoseDriver      每幀寫 cf_j_* 旋轉（T-pose 或 UE walk）
        └─ FOkMouthDriver     兩個權重 + 牙/舌可見性
        │
        ▼
PoseableMesh bone local
GPU skin VS：pos = v_ref + w_close*d_close + w_open*d_open，再乘骨頭矩陣
```

`v_ref` 的定義已在 `skel_export.rs`：`v_ref = skin(v, restWorld, bind)`，GPU 做 `posed * inv(rest) * v_ref` 才等於 Unity `boneWorld * bind * v`。嘴 delta 必須用同一個線性部分（`restWorld * bind`，不加平移）變到這個空間，否則方向錯。

## 4. 交接資料（每個 outfit 一次）

抽完可以 `session_free`。熱路徑不留 session。

| 塊 | 內容 | 來源 |
|---|---|---|
| 骨架 | 名字、parent、T-pose rest local、layer | `skel_export::build`。layer 1 = `cf_j_*`，layer 2 = `cf_s_*`/`cf_n_*`/`cf_d_*`/`cf_hit_*` |
| 蒙皮部件 | `v_ref`、法線、UV、索引、權重、共享骨索引、材質 | 同左。`skel_kind` 1 身體、2 頭 |
| 沒蒙皮的飾品 | parent 骨名 + prefab local | `parent_bone`。掛 socket，不要再 skin |
| 形變原料 | body/head key、`correct[]`、`type_bone`、性別、`nipStandRate`、卡片 ABM、初始 `shapeValueBody[44]` / `shapeValueFace[52]` | session 已有。分類表 `cf_custombody.txt` / `cf_customhead.txt` 是常數，編進 C++，不必塞 blob |
| 嘴巴 | pattern 0–4 的 close/open 稀疏 delta，已在 `v_ref` 空間 | `mouth_ptn_pair` + blendshape。眼睛／眉毛中性 FBS 抽取時烤進 `v_ref`，不做成滑桿 |

### 頭髮（phase 1 的洞）

`rig_mesh` 的頭髮 **沒有** 設 `skel_kind`。它對 `head_full_skel`（`cf_J_hairF_top` 那棵，臉 SMR 的 45 骨沒有這些關節）做 CPU skin，再 `apply_parent`。`skel_export` 只合併 `body_rest` + `head_rest`（掛在 `cf_s_head` 下）。

所以現在不能把頭髮標成 `skel_kind = 2` 丟進共享骨架：骨索引對不上，會捏爛。C++ 裡 `!bHairPart` 把頭髮排除在 GPU 之外，是這個洞的症狀，不是解法。

要做的事（有依據，不是新蒙皮公式）：

1. session 留一份 `head_full` rest（`skeleton_full_named("cf_J_hairF_top")`，形變之前的那份）。
2. `skel_export::build` 把這棵裡、共享骨架還沒有的骨補進去。parent 用名字接到已有的頭骨（遊戲 `CopySameNameTransform` / `LoadCharaFbxData` 同名 remap）。
3. 頭髮 `remap_part_to_body` 到這棵，`skel_kind` 指向它，**不要** 把 skinned world 寫進 `rest_positions`。`bake_part` 用 bind 頂點做 `v_ref`。
4. 沒有權重、但有 `parent_bone` 的飾品（含頭髮飾品）掛 socket。腳下那坨沒貼圖的灰塊是這格漏了，或 bind 頂點留在原點。

在 2–3 完成前，頭髮可以暫時用抽完的 world 頂點當靜態 PMC（現況）。它不會跟著滑桿或走路。驗收「頭髮留在頭上並且跟著骨架」以 2–3 為準。

## 5. 每幀（C++）

```
OnShapeChanged:
  寫 shapeValueBody[i] 或 shapeValueFace[i]   // 內部 0–1，UI 顯示 0–100
  dirty = true                                 // 不在事件裡 Update

Tick:
  if dirty:
    ShapeSolver.Update()                       // 身體 → 讀 cf_s_head 世界縮放 → 臉 → ABM
    dirty = false
  PoseDriver.Apply()                           // T-pose：cf_j_* = rest 旋轉；走路：只寫對得到的 cf_j_* 旋轉
  MouthDriver.Apply()                          // 兩個權重；open>0 顯示牙/舌
  把 local 推進所有 SkelFollowers
  不呼叫 Session.Skin()
```

`UpdateAlways` 的肩膀 X 每幀寫，便宜，跟遊戲一樣。不要等 dirty。

ABM 在 `Update()` 之後、姿勢旋轉之前，沿用 `pose::apply_abm` 的加量。若 ABM 打到 `cf_j_*` 的 position/scale，走路只覆蓋那些骨的旋轉。

## 6. C++ 物件

| 物件 | 職責 | 檔案 |
|---|---|---|
| `FOkLiveBlob` | 交接後的骨架、部件、key、嘴 delta。session 關掉仍在 | 新，`Plugins/KKUnityBundle` |
| `FOkShapeSolver` | `ChangeValue` + `body_update` + `head_update` + `apply_dst` + 肩膀 + ABM。移植 `shape.rs`，不重推 | 新 |
| `FOkPoseDriver` | `EOkPoseMode::{TPose, UeWalk}`。走路用現有骨名對照，但是 `shaped` 的 `cf_j_*` 只換旋轉 | `KKSkeletalBuilder.cpp` 的 `OKOverlayUeWalk` 要改掉 `KkRest * Delta` 整段覆蓋 |
| `FOkMouthDriver` | pattern + open。權重公式見下 | 新 |
| `AKKPreviewActor::Tick` | 上面三個的唯一呼叫點 | `KKPreviewActor.cpp` |

側欄 `OKGalleryWidget` 已有 96 支。改三處：初值讀卡片、顯示 0–100、`OnShapeChanged` 只設 dirty。

### 嘴巴權重

`FBSBase.CalculateBlendShape`，pattern 固定、不在混合兩型時：

```
open01 = clamp(openPercent / 100, 0, 1)
w_close = (1 - open01) * 100
w_open  = open01 * 100
```

`mouth_ptn_pair`（已轉錄，不要重編）：

| mesh | close[5] | open[5] |
|---|---|---|
| `cf_O_face` | 28, 31, 32, 31, 31 | 29, 30, 33, 34, 35 |
| `cf_O_noseline` | 25, 28, 29, 28, 28 | 26, 27, 30, 31, 32 |
| `cf_O_tooth` / `cf_O_canine` | 0, 0, 4, 0, 0 | 1, 2, 5, 6, 7 |
| `o_tang` | 0, 3, 4, 3, 3 | 1, 2, 5, 6, 7 |

demo 預設 pattern 0（微笑）+ 開合滑桿。另外四型換 delta，不換公式。`open = 0` 仍要套 close = 100，否則嘴唇不合。

GPU：skin 前的 deformer（或已確認不會回 CPU 的 5.8 GPU morph）做 `pos += w_close*d_close + w_open*d_open`，再進現有骨頭矩陣。每幀只上傳兩個 float。不要 `UpdateMorphedVertexBuffer`。

牙／舌／犬齒 mesh 一律在 blob 裡。可見性是元件旗標，不是再抽一次。

### 走路

骨名對照已在 `OKOverlayUeWalk`（pelvis→`cf_j_hips`、spine、四肢）。保留對照，改合成：

```
cf_j_* .rotation = WalkDelta          // WalkRest^-1 * WalkPose，換到 KK 軸之後
cf_j_* .scale / position 不動         // 那些若被 ABM 寫過就留著
cf_s_* / cf_d_* / 臉骨 完全不寫
```

比例差很大時步伐會漂（mannequin 腿長）。那是這層重定向的極限，不在這一輪做 IK Retargeter。哪支 `AnimSequence` 在 phase 4 定，不擋前三步。

T-pose = 遊戲 rest（mesh 的 bind），不是再套一次 mannequin T-pose。`OK_NOPOSE` 已是這條。

## 7. CPU 除錯

`OK_CPU_SKIN=1` 才跑。同一組骨頭 LBS，對 Rust `apply_shape` + `skin_mesh`（嘴再加 `apply_default_fbs`）。

- 形變：卡片現值，以及身高 0 / 1、胸 0 / 1。bone local 最大誤差 `< 1e-4`。
- 嘴：open = 0、0.5、1，pattern 0。頂點對拍。
- 不過不准把 viewer 熱路徑切到 C++ solver / GPU 嘴。

顯示路徑不讀這個結果。

## 8. 階段

### Phase 1 — 顯示改走 GPU，預設 T-pose

狀態：live session 預設 GPU + T-pose。頭髮 `skel_kind = 3`，`head_full` 補進共享骨架。嘴巴 close/open 掛 `UMorphTarget`（UE 5.8 `GUseGPUMorphTargets` 預設 1，skin 前的 compute）。側欄有 T-pose / 走路 / 嘴巴開合。走路有 `OK_WALK_ANIM` 才做 UE 重定向（只改 `cf_j_*` 旋轉）；否則用遊戲 `f_aruki_00_00`，因為這台機器沒有 mannequin walk 資產。形變公式仍是 `shape.rs`（已對過遊戲），C++ 每幀只推骨頭、不 `Skin()`。第二份 C++ `body_update` 會和 oracle 分叉，所以不複製那 900 行。

live session（`FOkSession::Open`）才設，dump / cargo test 不設：

- `OK_GPU_SKIN=1`、`OK_SKELMESH=1`、`OK_NOPOSE=1`、`OK_UE_ANIM=1`
- `OKSetLiveGpuSkin(true)`，不依賴 `OKSkelMeshEnabled` 的一次性 env 快取

C++：

- `skel_kind` 1 和 2 一律建 `USkeletalMesh` + `PoseableMesh`（身體、頭、衣服、嘴）。
- `skel_kind` 0 且有 `parent_bone` 的掛到 socket，**含頭髮飾品**。不要把已經 bake 成 world 的頂點再掛一次（會雙重變換）。
- `ApplyShapes` 在已有 `SkelFollowers` 時：可以暫時仍呼叫 Rust `apply_shape`（phase 2 換掉）+ `RefreshSkinnedDisplay`，**禁止** `Session.Skin()` 和 PMC 重寫。
- `OnLoad3D` 不再 `StartAnimLoop`（那個會改播 `f_aruki_00_00`）。

頭髮 GPU 跟骨架：見 §4。沒完成前頭髮是靜態 PMC，要在 log 裡標 `hair static`。

驗收：載入 `KK_091207`，身體／頭／衣服是 PoseableMesh，沒有每幀 CPU skin。姿勢是 T-pose（手臂打開），不是 `Stand_D_00` 也不是遊戲走路。

### Phase 2 — ShapeSolver + 滑桿

- 移植 `shape.rs` 的取樣與 `Update`。oracle 不過不接熱路徑。
- 滑桿初值 = 卡片。顯示 0–100。拖動只設 dirty。
- 熱路徑不再呼叫 `apply_shape`。

驗收：拖身高、胸、臉寬，同一幀只見骨頭在動。走路還沒接也不重置。誤差門檻見 §7。

### Phase 3 — 嘴巴進 GPU skin

- 匯出 `v_ref` 空間的 close/open delta。
- deformer / GPU morph，不 CPU 寫頂點。
- 開合滑桿。pattern 0。`open>0` 顯示牙／舌。

驗收：合嘴嘴唇合上；張開看得到牙和舌，並且跟頭一起被 skin。拖嘴巴時不做 CPU 頂點迴圈。同時拖身高，嘴型還在。

### Phase 4 — 姿勢開關

- 側欄 `T-pose` / `走路`。
- 走路中拖身高：人變矮，步伐不重置，嘴型還在。
- `OKOverlayUeWalk` 改成只寫旋轉。

## 9. 檔案

| 改 | 不改用途 |
|---|---|
| `GPUSkin.md` | 本規格 |
| `Tools/ok-extract/src/skel_export.rs` | 補頭髮骨、嘴 delta 空間 |
| `Tools/ok-extract/src/lib.rs` `rig_mesh` 頭髮分支 | 只在 `OK_GPU_SKIN` 時改，dump 路徑維持 CPU skin |
| `Tools/ok-extract/src/shape.rs` | 不刪。oracle |
| `Plugins/KKUnityBundle/.../KKSkeletalBuilder.*` | live GPU 旗標、走路合成 |
| `Plugins/KKUnityBundle/.../KKPreviewActor.*` | Tick、不再 CPU skin 顯示 |
| `Plugins/KKUnityBundle/.../KKRustExtract.cpp` | `Open` 前設 live env |
| `Source/OpenKoikatsu/OKGalleryWidget.cpp` | 滑桿 dirty、不自動走路 |
| 新 `FOkShapeSolver` / `FOkMouthDriver` / `FOkLiveBlob` | phase 2–3 |

`EXTRACT_CACHE_VER`：沒改磁碟抽取邏輯就不要 bump。`OK_GPU_SKIN` 只影響 session。

## 10. 現況缺口（實作時不要踩）

- `OKSkelMeshEnabled` 把 env 快取死。live 路徑用 `OKSetLiveGpuSkin`，不要只設環境變數。
- `StartAnimLoop` 清掉 `WalkSeq` 並強制 `f_aruki_00_00`。demo 預設不能呼叫它。
- `ApplyShapes` 一律 `Session.Skin()` 再寫 PMC。GPU 顯示時這條是頓卡來源。
- `OKOverlayUeWalk` 用 `KkRest * Delta` 覆蓋 local，會清掉 scale。phase 4 改，phase 1 不要開走路。
- `OKAddMorphs` 未呼叫，空間也不對。phase 3 不要接上就當完成。
- 頭髮骨不在 `skel_export` 裡。見 §4。

## 11. Stage287：使用者新增乳搖需求（2026-09-28）

本節取代早期胸部DynamicBone不列範圍的限制。來源Ver02 solver已由bundle
讀取骨鏈／reference／pattern／限制與碰撞，依卡片size／softness／weight運作。
一般走路改走來源Studio controller state與通用game-animation更新，
body及衣物DynamicBone之後才發布同一GPU palette；不能再以顯示骨架rotation
overlay冒充有物理的來源動作。顯式OK_WALK_ANIM mannequin重定向尚未接此物理。

五卡900負對照幀及900實際顯示幀通過，57240個GPU follower骨骼比對誤差0cm。
驗收／重現／限制見Docs/UNREAL_BODY_DYNAMICS.md與Saved/Logs/BodyDynamics287Audit.json。

## 12. Stage288：外部Unreal動畫也沿來源物理（2026-09-29）

本節取代§11中顯式OK_WALK_ANIM尚未接物理的狀態。SetUnrealAnimation／
PlayAnimSequence先從卡片未模擬baseline重定向真UAnimSequence，送入source
Animator local pose，再依來源順序處理shape／ABMX／Ver02／衣物／GPU palette／
私有髮與配件。StartAnimLoop保留當前動作；production不再用post-physics
OKOverlayUeWalk。Missing tracks保留卡片比例，完整XYZ旋轉、骨盆位移與scale
有通用映射，沒有按卡名offset／振幅。

五卡source入口900幀、539100骨与19528380附件頂點差0；30invalid frames拒絕。
12種UE測試通過，外部900負對照幀與900真顯示幀、57240follower比對差0cm，
15圖／71附件audits通過。結果Saved/Logs/ExternalBody288Audit.json，
圖Saved/DebugShots/ExternalBody288_open，流程與限制Docs/UNREAL_BODY_DYNAMICS.md。
287截圖覆寫事故與重新產生的證據已記E-144及287 audit recovery欄位。

## 13. Stage289：來源1D／巢狀BlendTree進同一palette（2026-09-29）

controller reader不再將所有多motion states排除。1D／巢狀1D依serialized
thresholds與真clip時序混合card-shaped local TRS，然後沿source helpers／ABMX／
Ver02／衣物／GPU發布與私有髮、配件更新。參數不是按卡名比例補丁，也沒有
在物理之後更換GPU顯示姿勢。Gallery float sliders與Blueprint API共用Session。

五卡native300frames、81,000 animated TRS屬性樣本最大分量差<2e-7；
13種UE回歸含source／external physics，各900負對照frames，新增混合450frames。
15真RHI圖、71附件display audits差0cm與實際Gallery callbacks驗證。
1D範圍、2D／layers等尚未涵蓋項與重現見Docs/UNREAL_ANIMATOR_BLENDTREES.md；
驗收Saved/Logs/Animator289Audit.json，錯誤E-145。下巴仍依使用者要求略過。

## 14. Stage290：來源Cartesian與mask layer進同一palette（2026-09-29）

新增type3與nested1D→2D，依source pair／neighbor常數混合local TRS；single explicit
Override以actual Animator relative-path mask與default base組合，再進helpers／ABMX／
Ver02／衣物／private髮及配件／GPU同一流程，沒有post-physics顯示overlay。
五卡native720frames／194400 animated TRS屬性樣本最大誤差<2e-7，1D300原生frames
保持。UE14項回歸、新300layeredframes與source／external各900負對照PASS。
真RHI15圖／71附件0cm、Gallery四callbacks通過。躺姿preview改head-oriented
portrait与camera-up fit，floor移到actual bounds下方；角色TRS與palette未改。
Saved/Logs/Animator290Audit.json、Docs/UNREAL_ANIMATOR_2D.md、ERROR_LOG E-146
記完整來源、驗收與single Override範圍；automatic多層／additive／IK等尚未實作。


## 15. Stage291：真逐幀參數與來源入口（2026-09-29）

逐幀Session現在傳anim_parameters；共用C++ SetAnimClip成功時清Open的Tpose／external旗標，所有caller沿同一入口。修正不改source raw公式、helpers／ABMX／Ver02／clothes／private附件／GPU發布順序。舊UI getter／finite-only證據不可當參數已進完整palette，stage291以FFI實際播放與獨立external-entry控制核對五卡1020frames、610980 body transforms、22132164附件vertices，誤差0。

UE最終15tests及五卡15參數positive／restore控制PASS，真1D450／Cartesian300來源frames、source／external各900物理負對照通过；30真RHI圖逐張目視、每組71附件0cm。1D Gallery骨架642變化；Cartesian物理控制骨骼先scope隔離測X/Y到local／world發布，再恢復物理才拍照，完整Session／RHI持續含物理。Ver02 reference writer遵守game InitTransforms，不為了顯示滑桿效果覆寫最終physics pose。

Docs/UNREAL_ANIMATOR_RUNTIME.md、ERROR_LOG E-147、Saved/Logs/Animator291Audit.json記重現、失敗、SHA256及尚未支援的完整Animator／HAnime初始化範圍；下巴略過。
