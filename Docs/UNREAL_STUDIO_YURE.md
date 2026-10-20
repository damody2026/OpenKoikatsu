# Unreal Studio Yure 初始化（stage294，2026-09-29 已驗收）

## 來源與載入順序

`Studio/OCIChar.cs` 的 LoadAnime、`YureCtrl.cs`、`ChaControl.UpdateShapeBody`／`playDynamicBoneBust`／`ReSetupDynamicBoneBust`、`ShapeBodyInfoFemale.Update`、`DynamicBone_Ver02.OnEnable`／OnDisable／ResetPosition 是本輪依據。來源根目錄為 `C:/portable/KoikatuSunshine/kks_decomp/Managed/CharaStudio_Data/Assembly-CSharp`。

1. H 動作讀取目錄 options 第8/9欄指定的 Yure bundle/asset。LoadExcel 先 ResetShape，再依 state 名稱 Proc；重複 state rows 依序覆寫，最後一筆生效。
2. 四個開關依次為 BreastL、BreastR、HipL、HipR。左右形狀各9項：UDPos、LRRot、LRPos、UDRot、Sharpness、Shape、Bulge_Nip、Thickness_Nip、Sharpness_Nip。
3. 缺少 file／asset／state 使用來源全部 true 預設。普通 Studio 動作沒有 Yure reset，保留前一個 H 的政策；原生連續切換已確認。
4. ChaControl.UpdateShapeBody 先用完整 card 值更新 mask7，再將關閉的 shape_body[5..13] 換成 `[.5,.5,.5,.5,.5,.5,0,.5,.5]`，分側更新 mask1／2。nipStand 關閉時使用 .5，沒有 nipStandRate 插值。UpdateAlways 只處理 kokan/shoulder，沒有9種胸部 shape 每幀覆寫。
5. DynamicBone 停用當下 InitTransforms；後續停用影格跳過物理鏈，保留 Animator locals。重新啟用重設 Position／PrevPosition／ObjectPrevPosition，保留 UpdateTime。ResetPosition 更新胸部參考 locals／parameters，保留粒子歷史，沒有重建臀部鏈。

## 通用實作

- `anim/yure.rs` 讀取指定 bundle 中唯一同名資產，嚴格驗22個 bool及state身分。以原始 state short-name hash匹配，避免展示label suffix／clip葉節點名稱被誤作Yure state；hash衝突拒絕。base路徑與owned zipmod來源使用既有path／ZIP邊界檢查。
- mod row先使用該package的Yure bundle；該bundle缺少時可回到相同abdata相對路徑的base資產。跨modpackage的Yure相依與bundle覆蓋優先級尚未有原生控制，不猜另一個package。
- append C ABI `set_studio_animation_profile`；保留既有setter入口。先驗graph／options／Yure，再提交，失敗保留pose／policy。`studio_yure_flag` 是讀取當前22個flag的diagnostic API。
- Catalog、Gallery／換卡／換outfit、PreviewActor／ShotPreview共用profile入口。沒有角色名稱分支或體型offset。
- `shape.rs` 依mask1/2替換原本breast destination channels（L39..52／R53..66），沒有在每幀改寫Animator未要求的其它channels。
- runtime按準備元件的kind控制物理鏈。LoadExcel.ResetShape→Proc的enable順序保留，形狀切換只更新胸部參考／參數，保留臀部及粒子clock/history；GameColliders radius override保留。
- Yure是session動作狀態，沒有存進角色卡cache。產生cache blob時還原card-shaped baseline，避免H臨時遮罩在換卡／重載後變成預設體型。
- 冷載入完整性由 body/head 各自的來源 rest 與 shaped baseline 判定。第一個動作直接選 H 時，Yure 先建立兩者；不能只建立 body 後讓 Animator 將 head 重設成 None。已完成初始化的切換仍保留既有物理歷史。

## 冷載入錯誤與負對照

原本 `set_yure_policy` 只建立 body_shaped；`apply_anim_frame` 卻只以 body_shaped 判定是否需要初始化。GPU export 後第一個動作選 H，body_shaped 存在但 head_shaped 仍 None；下一影格 `reset_animator_pose` 清空 head_skel。EyeLook 的六種 pattern／Awake init 尚在，所有 source head／center／EyeTargets 卻消失，因此實際 RHI world-target gaze 失敗。單純看身體 palette 有限值、手動 UV gaze 或已預熱的 Rust Session 都無法發現。

`Studio294EyeLookColdDiagnostic.log` 保存真正冷載入 step0 失敗；`Studio294EyeLookMissingHead.log` 保存 UE 的 head=false／init=true 診斷。修正共用完整性判定，沒有修改 EyeLook 混合算法或以 identity 骨架兜底。新增五卡 cold GPU→H→24 body frames→60 source EyeLook steps→dt0 hold 控制；UE 的 Yure 測試也直接在第一個 H 選擇後驗 EyeLook。

第一輪修正後的 H 圖另發現黑色背景：舊背景只有固定 rear/side 牆與有限 floor，頭部跟隨鏡頭轉向前側時無牆覆蓋。共用 Studio 背景改六面灰色 enclosure，bounds 包含實際顯示幾何及三個 fitted cameras，再加空間餘量。所有拍攝線段的相機與角色端點都在同一凸盒內，牆不會擋住角色；拖曳／縮放也依目前相機重算。沿用原灰色材質與 color filter，沒有修改角色姿勢或光影算法。

## 原生驗證

五張卡：March、Alice、Yuuka、KK_091207、KK_384283。

- 每卡9次真 OCI.LoadAnime，normal→有Yure的H→normal→缺少Yure的H→normal→另一個有Yure state→normal→缺少Yure H→normal。讀取真正 DynamicBone.enabled 與 disableBustShapeMask；沒有手動替換原生flags。
- native shapeBones在ChaControl.UpdateShapeBody後立即抓取。只核對 ShapeBodyInfoFemale 寫入的胸部position／rotation／scale channels，保留不由shape寫入的動畫channels。
- 獨立 Ver02 controlled trace 每卡240frames，固定pose+root每幀移動 .001m；frame60關左胸、90關右胸、120關左臀、150開左胸、180開右胸／左臀。真正 OnEnable／OnDisable 與 source InitTransforms／UpdateDynamicBones 取樣，沒有以Rust結果建立expected。
- 數值算法採source captured baseline、初始particles／UpdateTime／reference locals／參數與colliders作輸入；比較每幀source輸出的10骨骼。這是enable lifecycle／Ver02算法控制，沒有宣稱完整自動Unity frame scheduler等价。
- 五卡1,200frames／12,000bone比較通過，最大TRS分量1.7022714e-5；門檻2e-5維持。

## Unreal 與完整 Session 驗收

| 檢查 | 結果 |
| --- | --- |
| 真 OCI.LoadAnime 政策／形狀 | 五卡45次切換，6,390個來源寫入屬性；最大分量1.1920929e-7，20個mask負對照、45個atomic invalid |
| Ver02 原生受控物理 | 1,200frames／12,000bones；max1.7022714e-5，tolerance2e-5 |
| 冷載入 H／視線 | 五卡直接GPU→H，300次EyeLook update及5次dt0保持；沒有預先播放其它動作 |
| Unreal queue | 18 distinct tests Success／QueueEmpty18／exit0；Yure450frames、五卡冷載入head／EyeTargets檢查、保留normal政策與missing fallback、atomic拒絕均通過 |
| 舊 Studio 完整 Session | 135frames，80,865 body TF／2,929,257 private附件vertices，最大誤差0；405 atomic controls |
| 真 RHI capture | 15張900×1080，71附件audit最大0cm，15fit≤.92；五卡gaze／highlight／world-target EyeLook通過，全部top row及全身四角灰底通過 |

Yure正對照是同H controller、同options的all-enabled Session；五卡均有最終palette差异。`max_delta`混合position與scale分量，不能當作公尺或乳房位移量。所有卡均為女性；本輪沒有男性原生對照。

18項queue使用完整初始化修正後的core build。之後僅修PreviewActor背景，再build成功10.34秒；最終15圖使用此build。沒有為背景修正重跑無關的原生物理算法，也沒有把上一版背景缺口的15圖當最終證據。

- 圖片：[五卡總覽](C:/portable/OpenKoikatsu/Saved/Tests/Studio294/ContactSheet.png)，完整圖在 `C:/portable/OpenKoikatsu/Saved/DebugShots/Studio294_OneD_open/`，只有本輪15圖與README。
- H `Stop_Idle` phase .4；固定world軸的全身圖是背面，face/side隨head orientation。不是T-pose或一般walk；Root Screenshots保留stage286參考集。
- `C:/portable/OpenKoikatsu/Saved/Logs/Studio294Audit.json` 保存fixture／來源檔／log／PNG SHA256；`Studio294Verified.log` 保存完整PASS。
- 初版背景圖在 `Saved/DebugShots/old/Studio294BeforeBackdrop`；原生初版trace／compiler／cold load／verifier失敗均保留，錯誤記於 `Docs/ERROR_LOG.md` E-150。
- 原生probe已移回Tools，game plugins沒有probe；owned UE測試33272／最終capture51756均正常exit0，沒有留下執行中的驗證process。

## 重跑

```powershell
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/build.ps1
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_yure_native.ps1 -Cards All
```

在 `C:/portable/OpenKoikatsu/Tools/ok-extract`，順序執行，避免Windows正在執行的同一test exe被linker覆寫：

```powershell
cargo test --release --lib source_rows_are_ordered_and_sides_are_independent -- --nocapture
cargo test --release --lib ver02_enable_transitions_keep_clock_and_disabled_animator_pose -- --nocapture
cargo test --release --lib ver02_game_enable_transition_trace -- --ignored --nocapture
cargo test --release --lib studio_yure_policy_and_shape_initialization -- --ignored --nocapture
cargo test --release --lib studio_yure_eye_look_diagnostic -- --ignored --nocapture
cargo test --release --lib studio_initialization_reaches_session_palette -- --ignored --nocapture
```

```powershell
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_yure.ps1 -Mode Tests
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_yure.ps1 -Mode Shots -Pose OneD -Blink open
C:/portable/OpenKoikatsu/Tools/lua/lua.exe C:/portable/OpenKoikatsu/Tools/GamePoseProbe/verify_yure.lua
```

## 範圍

完成目標為已匯入Studio row的Yure shape／enable政策，既有Ver02與髮片／附件共用時序。它不是完整H玩法、所有mod physics、碰撞場景或Unity自動scheduler驗收。

PV／FK/IK、option items、完整Studio scene、跨mod Yure dependencies、多active weighted layers／Additive／Directional／Mirror／IK／Timing=true／automatic transitions／CrossFade仍各自需要來源控制。下巴輪廓線依使用者要求延期。


## Stage295 對照修正

後續PV驗收發現各Session的palette index並非共用身份。舊KKStudioYure的兩Session同index對照已改為name join，驗唯一names、source完整group identity、source PV mask。最新五卡450frames／cold EyeLook／normal retain／fallback／atomic全部PASS；source shape effect正對照每卡1799–1800個更新，max .05–.489771。接受log為Saved/Logs/StudioYureUnrealAutomation295Console.log；完整證據見Docs/UNREAL_STUDIO_PV.md與HANDOFF2§152／ERROR_LOG E-151。stage294的獨立native shape／Ver02來源數據仍保留，沒有覆寫歷史audit。
