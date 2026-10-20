# Unreal Studio 動作初始化（stage293）

## 本輪範圍

從已匯入的 `DA_AnimationAndMouth`／`studio_catalog` 載入普通 Studio 和 H 動作。這輪完成來源 graph／runtime clip override／單一同步 Override layer／保存浮點選項的初始化；不是完整 Studio 場景或完整 H 物理初始化。

## 來源與根因

來源：`C:/portable/KoikatuSunshine/kks_decomp/Managed/CharaStudio_Data/Assembly-CSharp/Studio/OCIChar.cs` 的 `LoadAnime`，`Studio/Info.cs` 的 `HAnimeLoadInfo`，及 `Illusion/Utils.cs` 的 `SetupAnimatorOverrideController`。

1. LoadAnime 先載入 base bundle/controller，再以 override controller 的 animationClips 替換 clip。RuntimeAnimatorController 的名字變成 override 的名字，狀態機、參數、圖層仍來自 base。舊 Catalog 直接解析 override 的 graph，漏掉 base 的 79／73 等圖層。
2. Utils 依序執行 `aoc[clip.name] = clip`。名稱索引只命中原陣列第一張同名 original；其他同名 PPtr 保留。把所有同名葉節點都換掉會誤換胸部 motion。
3. 身高優先 `height1→height`；胸部優先 `Breast1→Breast→breast`，依完整 controller 的參數表判斷。H 載入尾端重新寫入保存的兩個 option，蓋過卡片身高／胸部 seed；普通 Studio 使用卡片身高。新 Studio 物件保存 options 初值為零。
4. H 的 `motion` 只在來源 `isMotion` 開啟時初始化。CSV 空 breastLayer 經來源 `int.TryParse(out)` 得到 0。解析完整 signed Int32，不能把 `79bad` 當 79；Bool.Parse 接受大小寫與首尾空白。
5. 同步 layer 的 `m_StateMachineSynchronizedLayerIndex` 指定該 state 的 motion set。使用 `m_BlendTreeConstantIndexArray[set]`；unset 才取 base tree。來源 `m_SyncedLayerAffectsTiming=false` 時，motion 使用 base 的 normalized phase。

## 通用實作

- `anim.rs` 的 Studio source descriptor 用 UTF-8 JSON 四欄（base source、base owner、overlay source、overlay owner）轉 HEX。完整 bundle／ZIP package 身分保留，避免 delimiter 混淆。Cache key 包含解析後來源根路徑。
- `controller.rs` 保留原始 full-path hash／short-name hash、controller/file/PPtr、浮點 default、motion set、mask、state clock。Runtime overlay 先建立 source original 陣列的 first-name→PPtr，再用 overlay 原陣列順序替換該 PPtr；不改 authored AnimatorOverrideController 的既有 PPtr 邏輯。
- Studio composed state 使用來源指定 layer，以及同 controller 的這次 base state。獨立 source layer preview 繼續保留原本語義；同步 state 只透過 composed Studio graph 公開，不能把它當成獨立 state 蓋在 authored Idle 上。公開 count／index／selectable API 使用相同過濾。
- `set_studio_animation` 在驗 source graph、float、clock、duration 後，一次提交 key／params／scene4／time0。失敗保留原 pose 與狀態。C++ FOkSession 同時切回來源取樣。
- Gallery、PreviewActor、匯入目錄的 ShotPreview 共用 Studio 入口。Gallery 換卡／換 outfit 重載時保存 Studio row 與 options；source combo 顯示簡短名稱，HEX 描述符只作內部 key。
- source controller 的 0..1 option 範圍是預覽控制範圍，不是 Unity serialized clamp；clock 範圍沿用 stage292。
- 沒有角色卡名稱分支。缺少／多重 source 身分、state、unsupported layer 組合會拒絕，沒有 bare-clip fallback。

## 原生證據

`Tools/GamePoseProbe/run_studio_native.ps1` 直接呼叫遊戲 `OCIChar.LoadAnime`，各五張卡 27 frames：

| 載入方式 | 遊戲目錄身分 | graph／layer |
|---|---|---|
| 普通 Studio | group0/category3/no0 | walk_run，base layer0 |
| Runtime override | group2/category128/no0 | khh_f_base + khh_f_28，layer79 |
| 多 motion set | group9/category183/no0 | khr_f_02_00，layer73，無 overlay |

每種三組保存 options × 三個 elapsed time。來源 clips／weights／length、controller 最終參數、full state hashes、animated TRS 都核對，physics 只在 raw 控制隔離。獨立 sampler 在 `Saved/Tests/Studio293Identity/`：base 與 layer clip 同名但 instance ID 不同；啟用 layer79 時 cf_d_bust00 qYZ=0，關閉後與 raw SampleAnimation 的 qYZ=0.00011583657／0.000132835732 一致。這證明問題是錯誤替換了同名 original，不能修成 shader、Quaternion rounding 或卡名補丁。

- Native：5 cards／135 frames／36,450 animated TRS attributes；position max1.1920929e-7、rotation max1.1920929e-7、scale max5.9604645e-8，原 tolerance2e-5。
- 移除 runtime override 的負對照：45 frames 確實改變 pose。
- 完整 Session：5 cards／135 frames／80,865 body transforms／2,929,257 runtime accessory vertices，最大誤差0；405 個 invalid-option atomic 控制。
- 完整 Session 以遊戲實際 LoadAnime 後的 params 作獨立输入，經 raw→external Animator 的共同 helpers／ABMX／已實作物理／附件路徑比較。不能用它宣稱尚未實作的 Yure enable／shape policy 已與遊戲一致。
- 最新 build 47.06 秒成功；17 種 UE tests 全 Success／QueueEmpty17／exit0。Studio 135 frames，10 個 H options positive 與45 atomic controls；原 source／external 乳搖各900 frames、1D／2D／clock、mod catalogue、手勢、眉眼／嘴型／gaze 回歸全部通過。
- 公開 synced-state 邊界與最新 March raw smoke 通過：450 個內部同步 states 不獨立公開，3560 個可選項的 count/index/selection 一致，Studio descriptor 的 Auto scene 正確為 Studio。
- 五卡15張900×1080 body／face／side圖在 `Saved/DebugShots/Studio293_OneD_open`；已逐列目視總覽，未見新增髮片／髮飾偏移。71附件 audits 誤差0cm、15fits≤.92、每卡gaze／eye highlight通過、側面上緣背景符合正面。總覽 `Saved/Tests/Studio293/ContactSheet.png`。
- 真 Gallery 匯入 row r00000022 callback 載入 composed walk，UI source 顯示 `Studio：walk_run`，height=.18 與mouth8呈現；截圖 `Saved/DebugShots/Studio293UI_OneD/gallery_imported_studio.png`。此輪没有額外slider或H換卡UI正對照，不把這張圖當作完整H UI驗證。
- `verify_studio.py` 完整 PASS，`Saved/Logs/Studio293Audit.json` 保存 native、source、logs、images SHA256；owned UE 77404／72548／67868 正常 exit0，遊戲 plugins 無probe。

## 重跑

```powershell
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/build.ps1
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_studio_native.ps1 -Cards All
```

在 `C:/portable/OpenKoikatsu/Tools/ok-extract`：

```powershell
cargo test --release --lib native_studio_load_graph_and_options -- --ignored --nocapture
cargo test --release --lib studio_initialization_reaches_session_palette -- --ignored --nocapture
cargo test --release --lib synchronized_states_require_studio_composition -- --ignored --nocapture
```

```powershell
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_studio.ps1 -Mode Tests
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_studio.ps1 -Mode Shots -Pose OneD -Blink open
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_studio.ps1 -Mode Gallery -Pose OneD
C:/portable/OpenKoikatsu/Tools/lua/lua.exe C:/portable/OpenKoikatsu/Tools/GamePoseProbe/verify_studio.lua
```

## 尚未完成

- YureExcel 每個 state 的四個 dynamic enable 與九種 L/R shape flags，以及切動作／disable／enable 時的物理 reference/history 更新。
- 完整 PV／FK/IK、動作 option item、Studio scene 保存欄位；本轮只保存已實作的 row/options。
- synced layer affects timing=true、同步到非 base state machine、多 active weighted layers、Additive、Directional 2D、Mirror、foot IK、automatic transitions／CrossFade。
- 所有第三方動畫 controller 的逐一原生驗證；既有 29,956 clip 等 TSV／UE DataTables 是資源清單，不等於全部玩法驗收。
- 下巴輪廓線依使用者指示暫不處理。


後續stage294已完成Yure四組enable、左右九shape政策與冷載入完整body/head基準，獨立原生及UE驗收見 `Docs/UNREAL_STUDIO_YURE.md`／`HANDOFF2.md` §151；本文件的stage293範圍與歷史SHA256維持。
