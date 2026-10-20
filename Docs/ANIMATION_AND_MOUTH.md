# Koikatsu Sunshine 動畫與嘴型資料總表

> 由 `Tools/anim_export/export_anim_mouth.lua` 自動產生。
> 資料來源：本機 Koikatsu Sunshine 安裝的 `abdata/` 與 `mods/*.zipmod`。
> 完整明細見同目錄（`Saved/anim_export/`）的 TSV／CSV。

本文件分兩部分：**動畫資料**（AnimationClip 清單、AnimatorController、Studio 動畫目錄）與**嘴型資料**（FaceBlendShape 的 Mouth／Eyes／Eyebrow 控制器、PtnSet 開閉配對、blendshape 名稱、lip-sync 機制）。

## 0. 摘要

- 掃描動畫 bundle：**116** 個（base 37 / mods 79）。
- AnimationClip 總數：**29,956**（base 25,857 / mods 4,099）。
- AnimatorController 總數：**529**。
- clip 長度：最短 0.033s，最長 232.000s，平均 1.725s（29,956 筆可計算長度）。
- clip 名稱去重後 **3,898** 個；跨 bundle 撞名最多的：M_SLoop1(×354), S_SLoop1(×353), L_SLoop1(×353), M_OLoop(×350), L_OLoop(×349)。

> 本文件與 TSV 是**資源清單／中繼資料**（clip 名稱、長度、曲線數、Studio 目錄、嘴型配對），不含逐格烘焙的骨骼曲線；逐格取樣由 `Tools/ok-extract/src/anim.rs` 於執行期完成。

## 1. 動畫資料

### 1.1 動畫存放位置與格式

角色的動作是 Unity **`AnimationClip`**，內含三種曲線來源：

| 區塊 | 說明 |
|---|---|
| `m_StreamedClip` | 稀疏關鍵影格；每個 key 有 `coeff[4]`（三次多項式 / 斜率 / 值） |
| `m_DenseClip` | 取樣後每格曲線值；`m_CurveCount × m_FrameCount` |
| `m_ConstantClip` | 固定值曲線 |

`m_ClipBindingConstant.genericBindings` 把曲線索引對到 Avatar 的 `m_TOS` 骨名 hash。時間由 `m_MuscleClip.m_StartTime` / `m_StopTime` 決定；`m_DenseClip` 另有自己的 `m_SampleRate` / `m_BeginTime`（末端可能有 padding）。

本專案可載入的動畫來源（`Tools/ok-extract/src/anim.rs` 的 `clip_sources` 與 `AssetBundleNames.cs`）：

| 目錄 | 用途 |
|---|---|
| `abdata/custom/custompose.unity3d` | Maker 姿勢（`custom_pose` AnimatorController） |
| `abdata/action/animator/00.unity3d` | 校園／日常動作 |
| `abdata/studio/anime/*.unity3d` | Studio 角色動作（單檔數千 clip） |
| `abdata/studio_k/anime/*.unity3d` | Studio KKS 追加動作 |
| `abdata/h/anim/{female,male,parts}` | H 動作（男女分開） |
| `abdata/esthetic/animator/00.unity3d` | 按摩動作 |
| `abdata/adv/motion/controller/adv/00.unity3d` | ADV 劇情動作 |
| `abdata/twoshot/*` | 雙人動作 |

### 1.2 動畫來源清單

| origin | source | clips | controllers | override controllers |
|---|---|---|---|---|
| base | abdata/action/animator/00.unity3d | 497 | 2 | 41 |
| base | abdata/adv/motion/controller/adv/00.unity3d | 145 | 4 | 0 |
| base | abdata/custom/bgmlist/00.unity3d | 0 | 0 | 0 |
| base | abdata/custom/colorsample.unity3d | 0 | 0 | 0 |
| base | abdata/custom/cos_def_f_00.unity3d | 0 | 0 | 0 |
| base | abdata/custom/cos_wed_f_20.unity3d | 0 | 0 | 0 |
| base | abdata/custom/cos_wed_m_20.unity3d | 0 | 0 | 0 |
| base | abdata/custom/custom_etc.unity3d | 0 | 0 | 0 |
| base | abdata/custom/custompose.unity3d | 70 | 1 | 0 |
| base | abdata/custom/customscenelist/00.unity3d | 0 | 0 | 0 |
| base | abdata/custom/customscenelist/01.unity3d | 0 | 0 | 0 |
| base | abdata/custom/customscenelist/add00.unity3d | 0 | 0 | 0 |
| base | abdata/custom/customscenelist/add01.unity3d | 0 | 0 | 0 |
| base | abdata/custom/presets_f_00.unity3d | 0 | 0 | 0 |
| base | abdata/custom/presets_m_00.unity3d | 0 | 0 | 0 |
| base | abdata/custom/samplevoice_00.unity3d | 0 | 0 | 0 |
| base | abdata/esthetic/animator/00.unity3d | 12 | 2 | 0 |
| base | abdata/h/anim/female/00_01_00.unity3d | 886 | 12 | 0 |
| base | abdata/h/anim/female/01_01_00.unity3d | 2,735 | 48 | 0 |
| base | abdata/h/anim/female/02_01_00.unity3d | 5,005 | 53 | 0 |
| base | abdata/h/anim/female/03_01_00.unity3d | 445 | 6 | 0 |
| base | abdata/h/anim/female/04_01_00.unity3d | 337 | 2 | 0 |
| base | abdata/h/anim/female/05_01_00.unity3d | 595 | 6 | 0 |
| base | abdata/h/anim/female/06_01_00.unity3d | 480 | 8 | 0 |
| base | abdata/h/anim/female/07_01_00.unity3d | 1,285 | 13 | 0 |
| base | abdata/h/anim/male/01_01_00.unity3d | 1,755 | 48 | 0 |
| base | abdata/h/anim/male/02_01_00.unity3d | 4,680 | 53 | 0 |
| base | abdata/h/anim/male/06_01_00.unity3d | 240 | 4 | 0 |
| base | abdata/h/anim/male/07_01_00.unity3d | 390 | 5 | 0 |
| base | abdata/h/anim/parts/01_00.unity3d | 542 | 8 | 0 |
| base | abdata/studio/anime/00.unity3d | 5,006 | 135 | 0 |
| base | abdata/studio/anime/01.unity3d | 242 | 3 | 0 |
| base | abdata/studio_k/anime/00.unity3d | 412 | 18 | 0 |
| base | abdata/studio_k/anime/04.unity3d | 66 | 2 | 0 |
| base | abdata/studio_k/anime/06.unity3d | 32 | 4 | 0 |
| base | abdata/twoshot/list/00.unity3d | 0 | 0 | 0 |
| base | abdata/twoshot/prefab/capturedialog.unity3d | 0 | 0 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_action_part1.unity3d | 56 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_action_part2.unity3d | 67 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_action_part3.unity3d | 63 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_action_part4.unity3d | 57 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_action_part5.unity3d | 85 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_action_part6.unity3d | 32 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_capoeira_part1.unity3d | 33 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_communication_part1.unity3d | 59 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_communication_part2.unity3d | 60 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_communication_part3.unity3d | 46 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_dance_part1.unity3d | 48 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_dance_part2.unity3d | 55 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_dance_part3.unity3d | 26 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_excercise_part1.unity3d | 63 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_fight_part1.unity3d | 68 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_fight_part2.unity3d | 49 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_fight_part3.unity3d | 33 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_gun_part1.unity3d | 53 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_gun_part2.unity3d | 69 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_gun_part3.unity3d | 49 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_idle_part1.unity3d | 52 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_jump_part1.unity3d | 54 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_pose_part1.unity3d | 77 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_soccer_part1.unity3d | 43 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_sports_part1.unity3d | 53 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_torch_part1.unity3d | 28 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_walknrun_part1.unity3d | 61 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_walknrun_part2.unity3d | 65 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_walknrun_part3.unity3d | 45 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_walknrun_part4.unity3d | 46 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_walknrun_part5.unity3d | 54 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_weapon_part1.unity3d | 55 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_wizard_part1.unity3d | 42 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_zombie_part1.unity3d | 39 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Armpitjob_F.unity3d | 30 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Cowgirl_leaning_F.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Floating_F.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Footjob_sitting_F.unity3d | 30 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Forward_bending_F.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Horizon_F.unity3d | 60 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Leg_hold_F.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Loop_halloween_F.unity3d | 60 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Reverse_cowgirl_F.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Reverse_lift_F.unity3d | 60 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/V_shell_F.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Armpitjob_M.unity3d | 30 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Cowgirl_leaning_M.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Floating_M.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Footjob_sitting_M.unity3d | 30 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Forward_bending_M.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Horizon_M.unity3d | 60 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Leg_hold_M.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Loop_halloween_M.unity3d | 60 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Reverse_cowgirl_M.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Reverse_lift_M.unity3d | 60 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/V_shell_M.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[johnbbob_la_petite]Custom Animations.zipmod!abdata/studio/anime/crap/female.unity3d | 52 | 8 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[johnbbob_la_petite]Custom Animations.zipmod!abdata/studio/anime/crap/male.unity3d | 39 | 7 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[johnbbob_la_petite]Custom Animations.zipmod!abdata/studio/anime/crap/un-human.unity3d | 1 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/female/Pose_1_F.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/female/Pose_2_F.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/female/Pose_3_F.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/female/Pose_4_F.unity3d | 30 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/female/Pose_5_F.unity3d | 60 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/female/Pose_6_F.unity3d | 30 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/male/Pose_1_M.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/male/Pose_2_M.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/male/Pose_3_M.unity3d | 78 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/male/Pose_4_M.unity3d | 30 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/male/Pose_5_M.unity3d | 60 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/male/Pose_6_M.unity3d | 30 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\kkstub.behind.zipmod!abdata/h/anim/female/custom_911_3.unity3d | 6 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\kkstub.behind.zipmod!abdata/h/anim/male/custom_911_3.unity3d | 6 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\kkstub.grind.zipmod!abdata/h/anim/female/custom_911_0.unity3d | 6 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\kkstub.grind.zipmod!abdata/h/anim/male/custom_911_0.unity3d | 6 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\kkstub.sideways.zipmod!abdata/h/anim/female/custom_911_1.unity3d | 3 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\kkstub.sideways.zipmod!abdata/h/anim/male/custom_911_1.unity3d | 3 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\kkstub.stickbug.zipmod!abdata/h/anim/female/stickbug.unity3d | 3 | 1 | 0 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Studio\AgiShark\[AgiShark] 2021 Christmas Bonus animations for CharaStudio v1.2.1.zipmod!abdata/studio/anime/2021christmas_bonus.unity3d | 65 | 1 | 0 |

### 1.3 每個來源的 clip 數分布

| origin | clip 總數 | bundle 數 |
|---|---|---|
| base | 25,857 | 37 |
| mod | 4,099 | 79 |

各 bundle 的 clip 數（由多到少，前 40 筆）：

| origin | source | clips |
|---|---|---|
| base | abdata/studio/anime/00.unity3d | 5,006 |
| base | abdata/h/anim/female/02_01_00.unity3d | 5,005 |
| base | abdata/h/anim/male/02_01_00.unity3d | 4,680 |
| base | abdata/h/anim/female/01_01_00.unity3d | 2,735 |
| base | abdata/h/anim/male/01_01_00.unity3d | 1,755 |
| base | abdata/h/anim/female/07_01_00.unity3d | 1,285 |
| base | abdata/h/anim/female/00_01_00.unity3d | 886 |
| base | abdata/h/anim/female/05_01_00.unity3d | 595 |
| base | abdata/h/anim/parts/01_00.unity3d | 542 |
| base | abdata/action/animator/00.unity3d | 497 |
| base | abdata/h/anim/female/06_01_00.unity3d | 480 |
| base | abdata/h/anim/female/03_01_00.unity3d | 445 |
| base | abdata/studio_k/anime/00.unity3d | 412 |
| base | abdata/h/anim/male/07_01_00.unity3d | 390 |
| base | abdata/h/anim/female/04_01_00.unity3d | 337 |
| base | abdata/studio/anime/01.unity3d | 242 |
| base | abdata/h/anim/male/06_01_00.unity3d | 240 |
| base | abdata/adv/motion/controller/adv/00.unity3d | 145 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_action_part5.unity3d | 85 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Cowgirl_leaning_F.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Floating_F.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Forward_bending_F.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Leg_hold_F.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/Reverse_cowgirl_F.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/female/V_shell_F.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Cowgirl_leaning_M.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Floating_M.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Forward_bending_M.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Leg_hold_M.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/Reverse_cowgirl_M.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\Omega\[Omega] Free_H_Animations_Bundles v1.1.1.zipmod!abdata/h/anim/male/V_shell_M.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/female/Pose_1_F.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/female/Pose_2_F.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/female/Pose_3_F.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/male/Pose_1_M.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/male/Pose_2_M.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\[ΩMEGA]\[[ΩMEGA]] Demo_H_Animations v1.0.8.zipmod!abdata/h/anim/male/Pose_3_M.unity3d | 78 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_pose_part1.unity3d | 77 |
| base | abdata/custom/custompose.unity3d | 70 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - Animations\AgiShark\[AgiShark] Mixamo animations for CharaStudio v3.1.2.zipmod!abdata/studio/anime/mixamo_gun_part2.unity3d | 69 |

## 2. Studio 動畫目錄（`abdata/studio/info/*.unity3d` + mods CSV）

Studio 的角色動作目錄由 `studio/info/*.unity3d` 的 `ExcelData` 提供，mod 則以 `abdata/studio/info/**/*.csv` 追加（Sideloader 轉成 ExcelData）。對應 `Studio/Info.cs` 的 `LoadAnimeLoadInfo` / `LoadHAnimeLoadInfo`。

目錄筆數：group 30、category 391、Anime 2,759、HAnime 5,992、HandAnime 42。

### 2.1 動作群組（group）

| group | 名稱 | origin |
|---|---|---|
| 0 | キャラ | base |
| 1 | H愛撫 | base |
| 2 | 女H奉仕 | base |
| 3 | 男H奉仕 | base |
| 4 | 女H挿入 | base |
| 5 | 男H挿入 | base |
| 6 | H特殊 | base |
| 7 | スタジオ | base |
| 8 | バトル | base |
| 9 | レズ | base |
| 10 | アウトドア | base |
| 11 | サマー | base |
| 12 | 汎用 | base |
| 13 | H女複数 | base |
| 14 | アフタースクール | base |
| 15 | EC | base |
| 16 | Hダークネス | base |
| 17 | サンシャイン | base |
| 19 | S・女H奉仕 | base |
| 20 | S・男H奉仕 | base |
| 21 | S・女H挿入 | base |
| 22 | S・男H挿入 | base |
| 23 | S・H特殊 | base |
| 24 | S・H3P | base |
| 911 | kkstub | mod |
| 1011 | Kohai | mod |

### 2.2 每個群組的動作數量

| group | 群組名稱 | categories | Anime | HAnime |
|---|---|---|---|---|
| 0 | キャラ | 16 | 308 | 0 |
| 1 | H愛撫 | 11 | 0 | 145 |
| 2 | 女H奉仕 | 35 | 0 | 353 |
| 3 | 男H奉仕 | 35 | 0 | 353 |
| 4 | 女H挿入 | 36 | 0 | 864 |
| 5 | 男H挿入 | 36 | 0 | 864 |
| 6 | H特殊 | 6 | 0 | 32 |
| 7 | スタジオ | 8 | 89 | 0 |
| 8 | バトル | 36 | 278 | 0 |
| 9 | レズ | 6 | 0 | 42 |
| 10 | アウトドア | 3 | 13 | 0 |
| 11 | サマー | 2 | 23 | 0 |
| 12 | 汎用 | 4 | 58 | 0 |
| 13 | H女複数 | 9 | 0 | 198 |
| 14 | アフタースクール | 1 | 4 | 0 |
| 15 | EC | 1 | 1 | 0 |
| 16 | Hダークネス | 18 | 0 | 273 |
| 17 | サンシャイン | 10 | 113 | 0 |
| 19 | S・女H奉仕 | 13 | 0 | 121 |
| 20 | S・男H奉仕 | 13 | 0 | 121 |
| 21 | S・女H挿入 | 25 | 0 | 600 |
| 22 | S・男H挿入 | 25 | 0 | 600 |
| 23 | S・H特殊 | 2 | 0 | 14 |
| 24 | S・H3P | 18 | 0 | 339 |
| 40 |  | 0 | 0 | 194 |
| 50 |  | 0 | 0 | 856 |
| 60 |  | 0 | 10 | 0 |
| 99 |  | 0 | 5 | 16 |
| 911 | kkstub | 4 | 0 | 7 |
| 1011 | Kohai | 18 | 1850 | 0 |
| 6549 |  | 0 | 7 | 0 |

### 2.3 HAnime 欄位意義

`studio_catalog.tsv` 的 HAnime 欄位對應 `Studio/Info.cs` 的 `HAnimeLoadInfo`：

| 欄位 | 意義 |
|---|---|
| group / category / id | 群組、中間分類、管理番號 |
| name | 顯示名 |
| bundle / file | 基底動作 bundle 與 asset（AnimatorController） |
| override_bundle / override_file | 覆寫動作（實際 clip 所在的 override controller） |
| clip | Animator state 名（不是 AnimationClip 名） |
| options | BreastLayer、dynamic、isMotion、pv[4]、yure bundle/file |

### 2.4 手部動作（HandAnime）

| id | 名稱 | bundle | file | clip |
|---|---|---|---|---|
| 1 |  | studio/base/00.unity3d | cf_hand_R_00 | goo |
| 1 |  | studio/base/00.unity3d | cf_hand_L_00 | goo |
| 2 |  | studio/base/00.unity3d | cf_hand_R_00 | scissors |
| 2 |  | studio/base/00.unity3d | cf_hand_L_00 | scissors |
| 3 |  | studio/base/00.unity3d | cf_hand_R_00 | par |
| 3 |  | studio/base/00.unity3d | cf_hand_L_00 | par |
| 4 |  | studio/base/00.unity3d | cf_hand_R_00 | grip_hard |
| 4 |  | studio/base/00.unity3d | cf_hand_L_00 | grip_hard |
| 5 |  | studio/base/00.unity3d | cf_hand_R_00 | grip_normal |
| 5 |  | studio/base/00.unity3d | cf_hand_L_00 | grip_normal |
| 6 |  | studio/base/00.unity3d | cf_hand_R_00 | grip_soft |
| 6 |  | studio/base/00.unity3d | cf_hand_L_00 | grip_soft |
| 7 |  | studio/base/00.unity3d | cf_hand_R_00 | par_soft |
| 7 |  | studio/base/00.unity3d | cf_hand_L_00 | par_soft |
| 8 |  | studio/base/00.unity3d | cf_hand_R_00 | par_grip |
| 8 |  | studio/base/00.unity3d | cf_hand_L_00 | par_grip |
| 9 |  | studio/base/00.unity3d | cf_hand_R_00 | gun_grip |
| 9 |  | studio/base/00.unity3d | cf_hand_L_00 | gun_grip |
| 10 |  | studio/base/00.unity3d | cf_hand_R_00 | soft_grip |
| 10 |  | studio/base/00.unity3d | cf_hand_L_00 | soft_grip |
| 11 |  | studio/base/00.unity3d | cf_hand_R_00 | dirty_grip |
| 11 |  | studio/base/00.unity3d | cf_hand_L_00 | dirty_grip |
| 12 |  | studio/base/00.unity3d | cf_hand_R_00 | natural_pose |
| 12 |  | studio/base/00.unity3d | cf_hand_L_00 | natural_pose |
| 13 |  | studio/base/00.unity3d | cf_hand_R_00 | hug |
| 13 |  | studio/base/00.unity3d | cf_hand_L_00 | hug |
| 14 |  | studio/base/00.unity3d | cf_hand_R_00 | index_finger1 |
| 14 |  | studio/base/00.unity3d | cf_hand_L_00 | index_finger1 |
| 15 |  | studio/base/00.unity3d | cf_hand_R_00 | index_finger2 |
| 15 |  | studio/base/00.unity3d | cf_hand_L_00 | index_finger2 |
| 16 |  | studio/base/00.unity3d | cf_hand_R_00 | thumb_finger |
| 16 |  | studio/base/00.unity3d | cf_hand_L_00 | thumb_finger |
| 17 |  | studio/base/00.unity3d | cf_hand_R_00 | ok |
| 17 |  | studio/base/00.unity3d | cf_hand_L_00 | ok |
| 18 |  | studio/base/00.unity3d | cf_hand_R_00 | soft_hold |
| 18 |  | studio/base/00.unity3d | cf_hand_L_00 | soft_hold |
| 19 |  | studio/base/00.unity3d | cf_hand_R_00 | go |
| 19 |  | studio/base/00.unity3d | cf_hand_L_00 | go |
| 20 |  | studio/base/00.unity3d | cf_hand_R_00 | hard_hold |
| 20 |  | studio/base/00.unity3d | cf_hand_L_00 | hard_hold |
| 21 |  | studio/base/00.unity3d | cf_hand_R_00 | par_straight |
| 21 |  | studio/base/00.unity3d | cf_hand_L_00 | par_straight |

完整 Anime／HAnime 條目見 `Saved/anim_export/studio_catalog.tsv`。

## 3. 嘴型與表情資料（FaceBlendShape）

### 3.1 機制

頭部 prefab 上的 `FaceBlendShape` MonoBehaviour 有四個控制器 `BlinkCtrl` / `EyebrowCtrl` / `EyesCtrl` / `MouthCtrl`（皆繼承 `FBSBase`）。每個控制器的 `FBSTarget` 指向一個 `SkinnedMeshRenderer`（臉、鼻線、牙齒、犬齒、舌頭等），其 `PtnSet` 是一串 `Close` / `Open` blendshape 索引配對。

執行期 `FBSBase.CalculateBlendShape` 依「開合率」在 `Close`（權重 1-開）與 `Open`（權重 開）之間內插：

```
weight(Close) = 1 - open
weight(Open)  = open
```

- `ChaControl.ChangeMouthPtn(ptn)` → `FBSCtrlMouth.ChangePtn` 選第幾個 PtnSet。
- `ChaControl.ChangeMouthOpenMax(v)` / `ChangeMouthFixed` 設定開合上限與固定開度。
- 嘴型 21 / 22 會啟用舌頭狀態與寬度縮放（`UseAdjustWidthScale`）。
- SexFaces 等插件會在既有 `PtnSet` 後追加 morph（base 43 組 → 54 組），因此本專案以「名稱」而非固定索引查找（見 `Docs/ERROR_LOG.md` E-073 / E-076）。

**Lip-sync**：`ChaControl.UpdateBlendShapeVoice()` 每幀算 `voiceValue`：有預先烘焙的 `WavInfoData`（`WavInfoControl.LipDataCreate`，RMS 序列、`soundinfo.bytes`）就用時間取樣，否則用 `AudioAssist.GetAudioWaveValue(AudioSource)` 取 `GetOutputData` 的 RMS，平滑後交給 `FBSCtrlMouth.CalcBlend(openValue)`；`ChangeMouthOpenMax` 是開合上限。

### 3.2 FBS 控制器總覽

| origin | asset | prefab | controller | target | target_go | mesh | patterns |
|---|---|---|---|---|---|---|---|
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | EyebrowCtrl | 0 | cf_O_mayuge | cf_O_mayuge | 17 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | EyesCtrl | 0 | cf_O_face | cf_O_face | 28 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | EyesCtrl | 1 | cf_O_noseline | cf_O_noseline | 28 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | EyesCtrl | 2 | cf_Ohitomi_L | cf_Ohitomi_L | 28 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | EyesCtrl | 3 | cf_Ohitomi_R | cf_Ohitomi_R | 28 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | EyesCtrl | 4 | cf_O_eyeline | cf_O_eyeline | 28 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | EyesCtrl | 5 | cf_O_eyeline_low | cf_O_eyeline_low | 28 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | EyesCtrl | 6 | cf_O_namida_L | cf_O_namida_L | 28 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | EyesCtrl | 7 | cf_O_namida_M | cf_O_namida_M | 28 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | EyesCtrl | 8 | cf_O_namida_S | cf_O_namida_S | 28 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | MouthCtrl | 0 | cf_O_face | cf_O_face | 43 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | MouthCtrl | 1 | cf_O_noseline | cf_O_noseline | 43 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | MouthCtrl | 2 | cf_O_tooth | cf_O_tooth | 43 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | MouthCtrl | 3 | cf_O_canine | cf_O_canine | 43 |
| mod | C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | MouthCtrl | 4 | o_tang | o_tang | 43 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyebrowCtrl | 0 | cf_O_mayuge | cf_O_mayuge | 17 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 0 | cf_O_face | cf_O_face | 30 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 1 | cf_O_noseline | cf_O_noseline | 30 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 2 | cf_Ohitomi_L | cf_Ohitomi_L | 30 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 3 | cf_Ohitomi_R | cf_Ohitomi_R | 30 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 4 | cf_O_eyeline | cf_O_eyeline | 30 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 5 | cf_O_eyeline_low | cf_O_eyeline_low | 30 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 6 | cf_O_namida_L | cf_O_namida_L | 30 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 7 | cf_O_namida_M | cf_O_namida_M | 30 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 8 | cf_O_namida_S | cf_O_namida_S | 30 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | MouthCtrl | 0 | cf_O_face | cf_O_face | 43 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | MouthCtrl | 1 | cf_O_noseline | cf_O_noseline | 43 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | MouthCtrl | 2 | cf_O_tooth | cf_O_tooth | 43 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | MouthCtrl | 3 | cf_O_canine | cf_O_canine | 43 |
| mod | C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | MouthCtrl | 4 | o_tang | o_tang | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyebrowCtrl | 0 | cf_O_mayuge | cf_O_mayuge | 17 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 0 | cf_O_face | cf_O_face | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 1 | cf_O_noseline | cf_O_noseline | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 2 | cf_Ohitomi_L | cf_Ohitomi_L | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 3 | cf_Ohitomi_R | cf_Ohitomi_R | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 4 | cf_O_eyeline | cf_O_eyeline | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 5 | cf_O_eyeline_low | cf_O_eyeline_low | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 6 | cf_O_namida_L | cf_O_namida_L | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 7 | cf_O_namida_M | cf_O_namida_M | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyesCtrl | 8 | cf_O_namida_S | cf_O_namida_S | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | MouthCtrl | 0 | cf_O_face | cf_O_face | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | MouthCtrl | 1 | cf_O_noseline | cf_O_noseline | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | MouthCtrl | 2 | cf_O_tooth | cf_O_tooth | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | MouthCtrl | 3 | cf_O_canine | cf_O_canine | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_00 | MouthCtrl | 4 | o_tang | o_tang | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | EyebrowCtrl | 0 | cf_O_mayuge | cf_O_mayuge | 17 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | EyesCtrl | 0 | cf_O_face | cf_O_face | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | EyesCtrl | 1 | cf_O_noseline | cf_O_noseline | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | EyesCtrl | 2 | cf_Ohitomi_L | cf_Ohitomi_L | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | EyesCtrl | 3 | cf_Ohitomi_R | cf_Ohitomi_R | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | EyesCtrl | 4 | cf_O_eyeline | cf_O_eyeline | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | EyesCtrl | 5 | cf_O_eyeline_low | cf_O_eyeline_low | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | EyesCtrl | 6 | cf_O_namida_L | cf_O_namida_L | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | EyesCtrl | 7 | cf_O_namida_M | cf_O_namida_M | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | EyesCtrl | 8 | cf_O_namida_S | cf_O_namida_S | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | MouthCtrl | 0 | cf_O_face | cf_O_face | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | MouthCtrl | 1 | cf_O_noseline | cf_O_noseline | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | MouthCtrl | 2 | cf_O_tooth | cf_O_tooth | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | MouthCtrl | 3 | cf_O_canine | cf_O_canine | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_02 | MouthCtrl | 4 | o_tang | o_tang | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | EyebrowCtrl | 0 | cf_O_mayuge | cf_O_mayuge | 17 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | EyesCtrl | 0 | cf_O_face | cf_O_face | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | EyesCtrl | 1 | cf_O_noseline | cf_O_noseline | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | EyesCtrl | 2 | cf_Ohitomi_L | cf_Ohitomi_L | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | EyesCtrl | 3 | cf_Ohitomi_R | cf_Ohitomi_R | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | EyesCtrl | 4 | cf_O_eyeline | cf_O_eyeline | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | EyesCtrl | 5 | cf_O_eyeline_low | cf_O_eyeline_low | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | EyesCtrl | 6 | cf_O_namida_L | cf_O_namida_L | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | EyesCtrl | 7 | cf_O_namida_M | cf_O_namida_M | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | EyesCtrl | 8 | cf_O_namida_S | cf_O_namida_S | 30 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | MouthCtrl | 0 | cf_O_face | cf_O_face | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | MouthCtrl | 1 | cf_O_noseline | cf_O_noseline | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | MouthCtrl | 2 | cf_O_tooth | cf_O_tooth | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | MouthCtrl | 3 | cf_O_canine | cf_O_canine | 43 |
| base | abdata/chara/bo_head_00.unity3d | p_cf_head_03 | MouthCtrl | 4 | o_tang | o_tang | 43 |

### 3.3 基礎頭部（`abdata/chara/bo_head_00.unity3d`，prefab `p_cf_head_00`）嘴型全表

`bo_head_00` 內另有 `p_cf_head_02`, `p_cf_head_03` 等 prefab，PtnSet 結構相同（見 `fbs_patterns.tsv`）。

#### MouthCtrl

| target | mesh | ptn | close | close_name | open | open_name |
|---|---|---|---|---|---|---|
| 0 | cf_O_face | 0 | 28 | kuti_face.f00_def_cl | 29 | kuti_face.f00_def_op |
| 0 | cf_O_face | 1 | 31 | kuti_face.f00_bisyou_op | 30 | kuti_face.f00_egao_op |
| 0 | cf_O_face | 2 | 32 | kuti_face.f00_uresi_cl | 33 | kuti_face.f00_uresi_op |
| 0 | cf_O_face | 3 | 31 | kuti_face.f00_bisyou_op | 34 | kuti_face.f00_uresi_s_op |
| 0 | cf_O_face | 4 | 31 | kuti_face.f00_bisyou_op | 35 | kuti_face.f00_uresi_ss_op |
| 0 | cf_O_face | 5 | 36 | kuti_face.f00_doki_cl | 37 | kuti_face.f00_doki_s_op |
| 0 | cf_O_face | 6 | 38 | kuti_face.f00_doki_s_cl | 37 | kuti_face.f00_doki_s_op |
| 0 | cf_O_face | 7 | 39 | kuti_face.f00_doki_ss_cl | 40 | kuti_face.f00_doki_ss_op |
| 0 | cf_O_face | 8 | 41 | kuti_face.f00_ikari_cl | 42 | kuti_face.f00_ikari_op |
| 0 | cf_O_face | 9 | 41 | kuti_face.f00_ikari_cl | 43 | kuti_face.f00_ikari02_op |
| 0 | cf_O_face | 10 | 39 | kuti_face.f00_doki_ss_cl | 44 | kuti_face.f00_sinken02_op |
| 0 | cf_O_face | 11 | 45 | kuti_face.f00_sinken03_cl | 46 | kuti_face.f00_sinken03_op |
| 0 | cf_O_face | 12 | 45 | kuti_face.f00_sinken03_cl | 47 | kuti_face.f00_keno_op |
| 0 | cf_O_face | 13 | 39 | kuti_face.f00_doki_ss_cl | 48 | kuti_face.f00_sabisi_op |
| 0 | cf_O_face | 14 | 45 | kuti_face.f00_sinken03_cl | 49 | kuti_face.f00_aseri_op |
| 0 | cf_O_face | 15 | 50 | kuti_face.f00_human_cl | 70 | kuti_face.f00_o_l_op |
| 0 | cf_O_face | 16 | 39 | kuti_face.f00_doki_ss_cl | 51 | kuti_face.f00_akire_op |
| 0 | cf_O_face | 17 | 28 | kuti_face.f00_def_cl | 52 | kuti_face.f00_odoro_op |
| 0 | cf_O_face | 18 | 28 | kuti_face.f00_def_cl | 53 | kuti_face.f00_odoro_s_op |
| 0 | cf_O_face | 19 | 54 | kuti_face.f00_doya_cl | 55 | kuti_face.f00_doya_op |
| 0 | cf_O_face | 20 | 56 | kuti_face.f00_pero_cl | 56 | kuti_face.f00_pero_cl |
| 0 | cf_O_face | 21 | 28 | kuti_face.f00_def_cl | 57 | kuti_face.f00_name_op |
| 0 | cf_O_face | 22 | 28 | kuti_face.f00_def_cl | 58 | kuti_face.f00_kuwae_op |
| 0 | cf_O_face | 23 | 59 | kuti_face.f00_kisu_op | 59 | kuti_face.f00_kisu_op |
| 0 | cf_O_face | 24 | 60 | kuti_face.f00_name02_op | 60 | kuti_face.f00_name02_op |
| 0 | cf_O_face | 25 | 71 | kuti_face.f00_n_s_cl | 61 | kuti_face.f00_a_s_op |
| 0 | cf_O_face | 26 | 72 | kuti_face.f00_n_l_cl | 62 | kuti_face.f00_a_l_op |
| 0 | cf_O_face | 27 | 71 | kuti_face.f00_n_s_cl | 63 | kuti_face.f00_i_s_op |
| 0 | cf_O_face | 28 | 72 | kuti_face.f00_n_l_cl | 64 | kuti_face.f00_i_l_op |
| 0 | cf_O_face | 29 | 71 | kuti_face.f00_n_s_cl | 65 | kuti_face.f00_u_s_op |
| 0 | cf_O_face | 30 | 72 | kuti_face.f00_n_l_cl | 66 | kuti_face.f00_u_l_op |
| 0 | cf_O_face | 31 | 71 | kuti_face.f00_n_s_cl | 67 | kuti_face.f00_e_s_op |
| 0 | cf_O_face | 32 | 72 | kuti_face.f00_n_l_cl | 68 | kuti_face.f00_e_l_op |
| 0 | cf_O_face | 33 | 71 | kuti_face.f00_n_s_cl | 69 | kuti_face.f00_o_s_op |
| 0 | cf_O_face | 34 | 72 | kuti_face.f00_n_l_cl | 70 | kuti_face.f00_o_l_op |
| 0 | cf_O_face | 35 | 71 | kuti_face.f00_n_s_cl | 71 | kuti_face.f00_n_s_cl |
| 0 | cf_O_face | 36 | 72 | kuti_face.f00_n_l_cl | 72 | kuti_face.f00_n_l_cl |
| 0 | cf_O_face | 37 | 73 | kuti_face.f00_neko_cl | 74 | kuti_face.f00_neko_op |
| 0 | cf_O_face | 38 | 75 | kuti_face.f00_san_op | 75 | kuti_face.f00_san_op |
| 0 | cf_O_face | 39 | 76 | kuti_face.f00_niko_cl | 77 | kuti_face.f00_niko_op |
| 0 | cf_O_face | 40 | 78 | kuti_face.f00_mogu_op | 80 | kuti_face.f00_tabe_op |
| 0 | cf_O_face | 41 | 79 | kuti_face.f00_mogu_cl | 78 | kuti_face.f00_mogu_op |
| 0 | cf_O_face | 42 | 50 | kuti_face.f00_human_cl | 50 | kuti_face.f00_human_cl |
| 1 | cf_O_noseline | 0 | 25 | kuti_nose.nl00_def_cl | 26 | kuti_nose.nl00_def_op |
| 1 | cf_O_noseline | 1 | 28 | kuti_nose.nl00_bisyou_op | 27 | kuti_nose.nl00_egao_op |
| 1 | cf_O_noseline | 2 | 29 | kuti_nose.nl00_uresi_cl | 30 | kuti_nose.nl00_uresi_op |
| 1 | cf_O_noseline | 3 | 28 | kuti_nose.nl00_bisyou_op | 31 | kuti_nose.nl00_uresi_s_op |
| 1 | cf_O_noseline | 4 | 28 | kuti_nose.nl00_bisyou_op | 32 | kuti_nose.nl00_uresi_ss_op |
| 1 | cf_O_noseline | 5 | 33 | kuti_nose.nl00_doki_cl | 34 | kuti_nose.nl00_doki_s_op |
| 1 | cf_O_noseline | 6 | 35 | kuti_nose.nl00_doki_s_cl | 34 | kuti_nose.nl00_doki_s_op |
| 1 | cf_O_noseline | 7 | 36 | kuti_nose.nl00_doki_ss_cl | 37 | kuti_nose.nl00_doki_ss_op |
| 1 | cf_O_noseline | 8 | 38 | kuti_nose.nl00_ikari_cl | 39 | kuti_nose.nl00_ikari_op |
| 1 | cf_O_noseline | 9 | 38 | kuti_nose.nl00_ikari_cl | 40 | kuti_nose.nl00_ikari02_op |
| 1 | cf_O_noseline | 10 | 36 | kuti_nose.nl00_doki_ss_cl | 41 | kuti_nose.nl00_sinken02_op |
| 1 | cf_O_noseline | 11 | 42 | kuti_nose.nl00_sinken03_cl | 43 | kuti_nose.nl00_sinken03_op |
| 1 | cf_O_noseline | 12 | 42 | kuti_nose.nl00_sinken03_cl | 44 | kuti_nose.nl00_keno_op |
| 1 | cf_O_noseline | 13 | 36 | kuti_nose.nl00_doki_ss_cl | 45 | kuti_nose.nl00_sabisi_op |
| 1 | cf_O_noseline | 14 | 42 | kuti_nose.nl00_sinken03_cl | 46 | kuti_nose.nl00_aseri_op |
| 1 | cf_O_noseline | 15 | 47 | kuti_nose.nl00_human_cl | 67 | kuti_nose.nl00_o_l_op |
| 1 | cf_O_noseline | 16 | 36 | kuti_nose.nl00_doki_ss_cl | 48 | kuti_nose.nl00_akire_op |
| 1 | cf_O_noseline | 17 | 25 | kuti_nose.nl00_def_cl | 49 | kuti_nose.nl00_odoro_op |
| 1 | cf_O_noseline | 18 | 25 | kuti_nose.nl00_def_cl | 50 | kuti_nose.nl00_odoro_s_op |
| 1 | cf_O_noseline | 19 | 51 | kuti_nose.nl00_doya_cl | 52 | kuti_nose.nl00_doya_op |
| 1 | cf_O_noseline | 20 | 53 | kuti_nose.nl00_pero_cl | 53 | kuti_nose.nl00_pero_cl |
| 1 | cf_O_noseline | 21 | 25 | kuti_nose.nl00_def_cl | 54 | kuti_nose.nl00_name_op |
| 1 | cf_O_noseline | 22 | 25 | kuti_nose.nl00_def_cl | 55 | kuti_nose.nl00_kuwae_op |
| 1 | cf_O_noseline | 23 | 56 | kuti_nose.nl00_kisu_op | 56 | kuti_nose.nl00_kisu_op |
| 1 | cf_O_noseline | 24 | 57 | kuti_nose.nl00_name02_op | 57 | kuti_nose.nl00_name02_op |
| 1 | cf_O_noseline | 25 | 68 | kuti_nose.nl00_n_s_op | 58 | kuti_nose.nl00_a_s_op |
| 1 | cf_O_noseline | 26 | 69 | kuti_nose.nl00_n_l_op | 59 | kuti_nose.nl00_a_l_op |
| 1 | cf_O_noseline | 27 | 68 | kuti_nose.nl00_n_s_op | 60 | kuti_nose.nl00_i_s_op |
| 1 | cf_O_noseline | 28 | 69 | kuti_nose.nl00_n_l_op | 61 | kuti_nose.nl00_i_l_op |
| 1 | cf_O_noseline | 29 | 68 | kuti_nose.nl00_n_s_op | 62 | kuti_nose.nl00_u_s_op |
| 1 | cf_O_noseline | 30 | 69 | kuti_nose.nl00_n_l_op | 63 | kuti_nose.nl00_u_l_op |
| 1 | cf_O_noseline | 31 | 68 | kuti_nose.nl00_n_s_op | 64 | kuti_nose.nl00_e_s_op |
| 1 | cf_O_noseline | 32 | 69 | kuti_nose.nl00_n_l_op | 65 | kuti_nose.nl00_e_l_op |
| 1 | cf_O_noseline | 33 | 68 | kuti_nose.nl00_n_s_op | 66 | kuti_nose.nl00_o_s_op |
| 1 | cf_O_noseline | 34 | 69 | kuti_nose.nl00_n_l_op | 67 | kuti_nose.nl00_o_l_op |
| 1 | cf_O_noseline | 35 | 68 | kuti_nose.nl00_n_s_op | 68 | kuti_nose.nl00_n_s_op |
| 1 | cf_O_noseline | 36 | 69 | kuti_nose.nl00_n_l_op | 69 | kuti_nose.nl00_n_l_op |
| 1 | cf_O_noseline | 37 | 25 | kuti_nose.nl00_def_cl | 26 | kuti_nose.nl00_def_op |
| 1 | cf_O_noseline | 38 | 70 | kuti_nose.nl00_san_op | 70 | kuti_nose.nl00_san_op |
| 1 | cf_O_noseline | 39 | 71 | kuti_nose.nl00_niko_cl | 72 | kuti_nose.nl00_niko_op |
| 1 | cf_O_noseline | 40 | 29 | kuti_nose.nl00_uresi_cl | 41 | kuti_nose.nl00_sinken02_op |
| 1 | cf_O_noseline | 41 | 69 | kuti_nose.nl00_n_l_op | 29 | kuti_nose.nl00_uresi_cl |
| 1 | cf_O_noseline | 42 | 47 | kuti_nose.nl00_human_cl | 47 | kuti_nose.nl00_human_cl |
| 2 | cf_O_tooth | 0 | 0 | kuti_ha.ha00_def_cl | 1 | kuti_ha.ha00_def_op |
| 2 | cf_O_tooth | 1 | 0 | kuti_ha.ha00_def_cl | 2 | kuti_ha.ha00_egao_op |
| 2 | cf_O_tooth | 2 | 4 | kuti_ha.ha00_uresi_cl | 5 | kuti_ha.ha00_uresi_op |
| 2 | cf_O_tooth | 3 | 0 | kuti_ha.ha00_def_cl | 6 | kuti_ha.ha00_uresi_s_op |
| 2 | cf_O_tooth | 4 | 0 | kuti_ha.ha00_def_cl | 7 | kuti_ha.ha00_uresi_ss_op |
| 2 | cf_O_tooth | 5 | 8 | kuti_ha.ha00_doki_cl | 9 | kuti_ha.ha00_doki_s_op |
| 2 | cf_O_tooth | 6 | 10 | kuti_ha.ha00_doki_s_cl | 9 | kuti_ha.ha00_doki_s_op |
| 2 | cf_O_tooth | 7 | 11 | kuti_ha.ha00_doki_ss_cl | 12 | kuti_ha.ha00_doki_ss_op |
| 2 | cf_O_tooth | 8 | 13 | kuti_ha.ha00_ikari_cl | 14 | kuti_ha.ha00_ikari_op |
| 2 | cf_O_tooth | 9 | 13 | kuti_ha.ha00_ikari_cl | 15 | kuti_ha.ha00_ikari02_op |
| 2 | cf_O_tooth | 10 | 11 | kuti_ha.ha00_doki_ss_cl | 16 | kuti_ha.ha00_sinken02_op |
| 2 | cf_O_tooth | 11 | 17 | kuti_ha.ha00_sinken03_cl | 18 | kuti_ha.ha00_sinken03_op |
| 2 | cf_O_tooth | 12 | 17 | kuti_ha.ha00_sinken03_cl | 19 | kuti_ha.ha00_keno_op |
| 2 | cf_O_tooth | 13 | 11 | kuti_ha.ha00_doki_ss_cl | 20 | kuti_ha.ha00_sabisi_op |
| 2 | cf_O_tooth | 14 | 17 | kuti_ha.ha00_sinken03_cl | 21 | kuti_ha.ha00_aseri_op |
| 2 | cf_O_tooth | 15 | 22 | kuti_ha.ha00_human_cl | 37 | kuti_ha.ha00_e_s_op |
| 2 | cf_O_tooth | 16 | 11 | kuti_ha.ha00_doki_ss_cl | 23 | kuti_ha.ha00_akire_op |
| 2 | cf_O_tooth | 17 | 0 | kuti_ha.ha00_def_cl | 24 | kuti_ha.ha00_odoro_op |
| 2 | cf_O_tooth | 18 | 0 | kuti_ha.ha00_def_cl | 25 | kuti_ha.ha00_odoro_s_op |
| 2 | cf_O_tooth | 19 | 26 | kuti_ha.ha00_doya_cl | 27 | kuti_ha.ha00_doya_op |
| 2 | cf_O_tooth | 20 | 28 | kuti_ha.ha00_pero_cl | 28 | kuti_ha.ha00_pero_cl |
| 2 | cf_O_tooth | 21 | 0 | kuti_ha.ha00_def_cl | 29 | kuti_ha.ha00_name_op |
| 2 | cf_O_tooth | 22 | 0 | kuti_ha.ha00_def_cl | 30 | kuti_ha.ha00_kuwae_op |
| 2 | cf_O_tooth | 23 | 31 | kuti_ha.ha00_kisu_op | 31 | kuti_ha.ha00_kisu_op |
| 2 | cf_O_tooth | 24 | 32 | kuti_ha.ha00_name02_op | 32 | kuti_ha.ha00_name02_op |
| 2 | cf_O_tooth | 25 | 0 | kuti_ha.ha00_def_cl | 33 | kuti_ha.ha00_a_s_op |
| 2 | cf_O_tooth | 26 | 0 | kuti_ha.ha00_def_cl | 34 | kuti_ha.ha00_a_l_op |
| 2 | cf_O_tooth | 27 | 0 | kuti_ha.ha00_def_cl | 35 | kuti_ha.ha00_i_s_cl |
| 2 | cf_O_tooth | 28 | 0 | kuti_ha.ha00_def_cl | 36 | kuti_ha.ha00_i_l_cl |
| 2 | cf_O_tooth | 29 | 0 | kuti_ha.ha00_def_cl | 23 | kuti_ha.ha00_akire_op |
| 2 | cf_O_tooth | 30 | 0 | kuti_ha.ha00_def_cl | 23 | kuti_ha.ha00_akire_op |
| 2 | cf_O_tooth | 31 | 0 | kuti_ha.ha00_def_cl | 37 | kuti_ha.ha00_e_s_op |
| 2 | cf_O_tooth | 32 | 0 | kuti_ha.ha00_def_cl | 37 | kuti_ha.ha00_e_s_op |
| 2 | cf_O_tooth | 33 | 0 | kuti_ha.ha00_def_cl | 37 | kuti_ha.ha00_e_s_op |
| 2 | cf_O_tooth | 34 | 0 | kuti_ha.ha00_def_cl | 37 | kuti_ha.ha00_e_s_op |
| 2 | cf_O_tooth | 35 | 0 | kuti_ha.ha00_def_cl | 0 | kuti_ha.ha00_def_cl |
| 2 | cf_O_tooth | 36 | 0 | kuti_ha.ha00_def_cl | 0 | kuti_ha.ha00_def_cl |
| 2 | cf_O_tooth | 37 | 0 | kuti_ha.ha00_def_cl | 37 | kuti_ha.ha00_e_s_op |
| 2 | cf_O_tooth | 38 | 37 | kuti_ha.ha00_e_s_op | 37 | kuti_ha.ha00_e_s_op |
| 2 | cf_O_tooth | 39 | 0 | kuti_ha.ha00_def_cl | 38 | kuti_ha.ha00_niko_op |
| 2 | cf_O_tooth | 40 | 0 | kuti_ha.ha00_def_cl | 38 | kuti_ha.ha00_niko_op |
| 2 | cf_O_tooth | 41 | 0 | kuti_ha.ha00_def_cl | 0 | kuti_ha.ha00_def_cl |
| 2 | cf_O_tooth | 42 | 22 | kuti_ha.ha00_human_cl | 22 | kuti_ha.ha00_human_cl |
| 3 | cf_O_canine | 0 | 0 | kuti_yaeba.y00_def_cl | 1 | kuti_yaeba.y00_def_op |
| 3 | cf_O_canine | 1 | 0 | kuti_yaeba.y00_def_cl | 2 | kuti_yaeba.y00_egao_op |
| 3 | cf_O_canine | 2 | 4 | kuti_yaeba.y00_uresi_cl | 5 | kuti_yaeba.y00_uresi_op |
| 3 | cf_O_canine | 3 | 0 | kuti_yaeba.y00_def_cl | 6 | kuti_yaeba.y00_uresi_s_op |
| 3 | cf_O_canine | 4 | 0 | kuti_yaeba.y00_def_cl | 7 | kuti_yaeba.y00_uresi_ss_op |
| 3 | cf_O_canine | 5 | 8 | kuti_yaeba.y00_doki_cl | 9 | kuti_yaeba.y00_doki_s_op |
| 3 | cf_O_canine | 6 | 10 | kuti_yaeba.y00_doki_s_cl | 9 | kuti_yaeba.y00_doki_s_op |
| 3 | cf_O_canine | 7 | 11 | kuti_yaeba.y00_doki_ss_cl | 12 | kuti_yaeba.y00_doki_ss_op |
| 3 | cf_O_canine | 8 | 13 | kuti_yaeba.y00_ikari_cl | 14 | kuti_yaeba.y00_ikari_op |
| 3 | cf_O_canine | 9 | 13 | kuti_yaeba.y00_ikari_cl | 15 | kuti_yaeba.y00_ikari02_op |
| 3 | cf_O_canine | 10 | 11 | kuti_yaeba.y00_doki_ss_cl | 16 | kuti_yaeba.y00_sinken02_op |
| 3 | cf_O_canine | 11 | 17 | kuti_yaeba.y00_sinken03_cl | 18 | kuti_yaeba.y00_sinken03_op |
| 3 | cf_O_canine | 12 | 17 | kuti_yaeba.y00_sinken03_cl | 19 | kuti_yaeba.y00_keno_op |
| 3 | cf_O_canine | 13 | 11 | kuti_yaeba.y00_doki_ss_cl | 20 | kuti_yaeba.y00_sabisi_op |
| 3 | cf_O_canine | 14 | 17 | kuti_yaeba.y00_sinken03_cl | 21 | kuti_yaeba.y00_aseri_op |
| 3 | cf_O_canine | 15 | 22 | kuti_yaeba.y00_human_cl | 37 | kuti_yaeba.y00_e_l_op |
| 3 | cf_O_canine | 16 | 11 | kuti_yaeba.y00_doki_ss_cl | 23 | kuti_yaeba.y00_akire_op |
| 3 | cf_O_canine | 17 | 0 | kuti_yaeba.y00_def_cl | 24 | kuti_yaeba.y00_odoro_op |
| 3 | cf_O_canine | 18 | 0 | kuti_yaeba.y00_def_cl | 25 | kuti_yaeba.y00_odoro_s_op |
| 3 | cf_O_canine | 19 | 26 | kuti_yaeba.y00_doya_cl | 27 | kuti_yaeba.y00_doya_op |
| 3 | cf_O_canine | 20 | 28 | kuti_yaeba.y00_pero_cl | 28 | kuti_yaeba.y00_pero_cl |
| 3 | cf_O_canine | 21 | 0 | kuti_yaeba.y00_def_cl | 29 | kuti_yaeba.y00_name_op |
| 3 | cf_O_canine | 22 | 0 | kuti_yaeba.y00_def_cl | 30 | kuti_yaeba.y00_kuwae_op |
| 3 | cf_O_canine | 23 | 31 | kuti_yaeba.y00_kisu_op | 31 | kuti_yaeba.y00_kisu_op |
| 3 | cf_O_canine | 24 | 32 | kuti_yaeba.y00_name02_op | 32 | kuti_yaeba.y00_name02_op |
| 3 | cf_O_canine | 25 | 0 | kuti_yaeba.y00_def_cl | 33 | kuti_yaeba.y00_a_s_op |
| 3 | cf_O_canine | 26 | 0 | kuti_yaeba.y00_def_cl | 34 | kuti_yaeba.y00_a_l_op |
| 3 | cf_O_canine | 27 | 0 | kuti_yaeba.y00_def_cl | 36 | kuti_yaeba.y00_i_l_cl |
| 3 | cf_O_canine | 28 | 0 | kuti_yaeba.y00_def_cl | 36 | kuti_yaeba.y00_i_l_cl |
| 3 | cf_O_canine | 29 | 0 | kuti_yaeba.y00_def_cl | 23 | kuti_yaeba.y00_akire_op |
| 3 | cf_O_canine | 30 | 0 | kuti_yaeba.y00_def_cl | 23 | kuti_yaeba.y00_akire_op |
| 3 | cf_O_canine | 31 | 0 | kuti_yaeba.y00_def_cl | 37 | kuti_yaeba.y00_e_l_op |
| 3 | cf_O_canine | 32 | 0 | kuti_yaeba.y00_def_cl | 37 | kuti_yaeba.y00_e_l_op |
| 3 | cf_O_canine | 33 | 0 | kuti_yaeba.y00_def_cl | 37 | kuti_yaeba.y00_e_l_op |
| 3 | cf_O_canine | 34 | 0 | kuti_yaeba.y00_def_cl | 37 | kuti_yaeba.y00_e_l_op |
| 3 | cf_O_canine | 35 | 0 | kuti_yaeba.y00_def_cl | 0 | kuti_yaeba.y00_def_cl |
| 3 | cf_O_canine | 36 | 0 | kuti_yaeba.y00_def_cl | 0 | kuti_yaeba.y00_def_cl |
| 3 | cf_O_canine | 37 | 0 | kuti_yaeba.y00_def_cl | 37 | kuti_yaeba.y00_e_l_op |
| 3 | cf_O_canine | 38 | 37 | kuti_yaeba.y00_e_l_op | 37 | kuti_yaeba.y00_e_l_op |
| 3 | cf_O_canine | 39 | 0 | kuti_yaeba.y00_def_cl | 38 | kuti_yaeba.y00_niko_op |
| 3 | cf_O_canine | 40 | 0 | kuti_yaeba.y00_def_cl | 38 | kuti_yaeba.y00_niko_op |
| 3 | cf_O_canine | 41 | 0 | kuti_yaeba.y00_def_cl | 0 | kuti_yaeba.y00_def_cl |
| 3 | cf_O_canine | 42 | 22 | kuti_yaeba.y00_human_cl | 22 | kuti_yaeba.y00_human_cl |
| 4 | o_tang | 0 | 0 | kuti_sita.t00_def_cl | 1 | kuti_sita.t00_def_op |
| 4 | o_tang | 1 | 3 | kuti_sita.t00_bisyou_op | 2 | kuti_sita.t00_egao_op |
| 4 | o_tang | 2 | 4 | kuti_sita.t00_uresi_cl | 5 | kuti_sita.t00_uresi_op |
| 4 | o_tang | 3 | 3 | kuti_sita.t00_bisyou_op | 6 | kuti_sita.t00_uresi_s_op |
| 4 | o_tang | 4 | 3 | kuti_sita.t00_bisyou_op | 7 | kuti_sita.t00_uresi_ss_op |
| 4 | o_tang | 5 | 8 | kuti_sita.t00_doki_cl | 9 | kuti_sita.t00_doki_s_op |
| 4 | o_tang | 6 | 10 | kuti_sita.t00_doki_s_cl | 9 | kuti_sita.t00_doki_s_op |
| 4 | o_tang | 7 | 11 | kuti_sita.t00_doki_ss_cl | 12 | kuti_sita.t00_doki_ss_op |
| 4 | o_tang | 8 | 13 | kuti_sita.t00_ikari_cl | 14 | kuti_sita.t00_ikari_op |
| 4 | o_tang | 9 | 13 | kuti_sita.t00_ikari_cl | 15 | kuti_sita.t00_ikari02_op |
| 4 | o_tang | 10 | 11 | kuti_sita.t00_doki_ss_cl | 16 | kuti_sita.t00_sinken02_op |
| 4 | o_tang | 11 | 17 | kuti_sita.t00_sinken03_cl | 18 | kuti_sita.t00_sinken03_op |
| 4 | o_tang | 12 | 17 | kuti_sita.t00_sinken03_cl | 19 | kuti_sita.t00_keno_op |
| 4 | o_tang | 13 | 11 | kuti_sita.t00_doki_ss_cl | 20 | kuti_sita.t00_sabisi_op |
| 4 | o_tang | 14 | 17 | kuti_sita.t00_sinken03_cl | 21 | kuti_sita.t00_aseri_op |
| 4 | o_tang | 15 | 22 | kuti_sita.t00_human_cl | 35 | kuti_sita.t00_e_s_op |
| 4 | o_tang | 16 | 11 | kuti_sita.t00_doki_ss_cl | 23 | kuti_sita.t00_akire_op |
| 4 | o_tang | 17 | 0 | kuti_sita.t00_def_cl | 24 | kuti_sita.t00_odoro_op |
| 4 | o_tang | 18 | 0 | kuti_sita.t00_def_cl | 25 | kuti_sita.t00_odoro_s_op |
| 4 | o_tang | 19 | 26 | kuti_sita.t00_doya_cl | 27 | kuti_sita.t00_doya_op |
| 4 | o_tang | 20 | 28 | kuti_sita.t00_pero_cl | 28 | kuti_sita.t00_pero_cl |
| 4 | o_tang | 21 | 0 | kuti_sita.t00_def_cl | 29 | kuti_sita.t00_name_op |
| 4 | o_tang | 22 | 0 | kuti_sita.t00_def_cl | 30 | kuti_sita.t00_kuwae_op |
| 4 | o_tang | 23 | 31 | kuti_sita.t00_kisu_op | 31 | kuti_sita.t00_kisu_op |
| 4 | o_tang | 24 | 32 | kuti_sita.t00_name02_op | 32 | kuti_sita.t00_name02_op |
| 4 | o_tang | 25 | 0 | kuti_sita.t00_def_cl | 33 | kuti_sita.t00_a_s_op |
| 4 | o_tang | 26 | 0 | kuti_sita.t00_def_cl | 34 | kuti_sita.t00_a_l_op |
| 4 | o_tang | 27 | 0 | kuti_sita.t00_def_cl | 0 | kuti_sita.t00_def_cl |
| 4 | o_tang | 28 | 0 | kuti_sita.t00_def_cl | 0 | kuti_sita.t00_def_cl |
| 4 | o_tang | 29 | 0 | kuti_sita.t00_def_cl | 23 | kuti_sita.t00_akire_op |
| 4 | o_tang | 30 | 0 | kuti_sita.t00_def_cl | 23 | kuti_sita.t00_akire_op |
| 4 | o_tang | 31 | 0 | kuti_sita.t00_def_cl | 35 | kuti_sita.t00_e_s_op |
| 4 | o_tang | 32 | 0 | kuti_sita.t00_def_cl | 35 | kuti_sita.t00_e_s_op |
| 4 | o_tang | 33 | 0 | kuti_sita.t00_def_cl | 35 | kuti_sita.t00_e_s_op |
| 4 | o_tang | 34 | 0 | kuti_sita.t00_def_cl | 35 | kuti_sita.t00_e_s_op |
| 4 | o_tang | 35 | 0 | kuti_sita.t00_def_cl | 0 | kuti_sita.t00_def_cl |
| 4 | o_tang | 36 | 0 | kuti_sita.t00_def_cl | 0 | kuti_sita.t00_def_cl |
| 4 | o_tang | 37 | 0 | kuti_sita.t00_def_cl | 35 | kuti_sita.t00_e_s_op |
| 4 | o_tang | 38 | 35 | kuti_sita.t00_e_s_op | 35 | kuti_sita.t00_e_s_op |
| 4 | o_tang | 39 | 0 | kuti_sita.t00_def_cl | 18 | kuti_sita.t00_sinken03_op |
| 4 | o_tang | 40 | 0 | kuti_sita.t00_def_cl | 18 | kuti_sita.t00_sinken03_op |
| 4 | o_tang | 41 | 0 | kuti_sita.t00_def_cl | 0 | kuti_sita.t00_def_cl |
| 4 | o_tang | 42 | 22 | kuti_sita.t00_human_cl | 22 | kuti_sita.t00_human_cl |

#### EyesCtrl

| target | mesh | ptn | close | close_name | open | open_name |
|---|---|---|---|---|---|---|
| 0 | cf_O_face | 0 | 0 | eye_face.f00_def_cl | 1 | eye_face.f00_def_op |
| 0 | cf_O_face | 1 | 0 | eye_face.f00_def_cl | 0 | eye_face.f00_def_cl |
| 0 | cf_O_face | 2 | 2 | eye_face.f00_egao_cl | 3 | eye_face.f00_egao_op |
| 0 | cf_O_face | 3 | 2 | eye_face.f00_egao_cl | 2 | eye_face.f00_egao_cl |
| 0 | cf_O_face | 4 | 2 | eye_face.f00_egao_cl | 4 | eye_face.f00_bisyou_op |
| 0 | cf_O_face | 5 | 5 | eye_face.f00_winkl_op | 5 | eye_face.f00_winkl_op |
| 0 | cf_O_face | 6 | 6 | eye_face.f00_winkr_op | 6 | eye_face.f00_winkr_op |
| 0 | cf_O_face | 7 | 0 | eye_face.f00_def_cl | 7 | eye_face.f00_setunai_op |
| 0 | cf_O_face | 8 | 0 | eye_face.f00_def_cl | 8 | eye_face.f00_tere_op |
| 0 | cf_O_face | 9 | 0 | eye_face.f00_def_cl | 9 | eye_face.f00_ikari_op |
| 0 | cf_O_face | 10 | 0 | eye_face.f00_def_cl | 10 | eye_face.f00_sinken_op |
| 0 | cf_O_face | 11 | 0 | eye_face.f00_def_cl | 11 | eye_face.f00_tumara_op |
| 0 | cf_O_face | 12 | 0 | eye_face.f00_def_cl | 12 | eye_face.f00_kurusi_op |
| 0 | cf_O_face | 13 | 0 | eye_face.f00_def_cl | 13 | eye_face.f00_keno_op |
| 0 | cf_O_face | 14 | 0 | eye_face.f00_def_cl | 14 | eye_face.f00_sian_op |
| 0 | cf_O_face | 15 | 0 | eye_face.f00_def_cl | 15 | eye_face.f00_kanasi_op |
| 0 | cf_O_face | 16 | 0 | eye_face.f00_def_cl | 16 | eye_face.f00_naki_op |
| 0 | cf_O_face | 17 | 0 | eye_face.f00_def_cl | 17 | eye_face.f00_aseri_op |
| 0 | cf_O_face | 18 | 0 | eye_face.f00_def_cl | 18 | eye_face.f00_rakutan_op |
| 0 | cf_O_face | 19 | 0 | eye_face.f00_def_cl | 19 | eye_face.f00_komaru_op |
| 0 | cf_O_face | 20 | 0 | eye_face.f00_def_cl | 20 | eye_face.f00_doya_op |
| 0 | cf_O_face | 21 | 21 | eye_face.f00_gag | 21 | eye_face.f00_gag |
| 0 | cf_O_face | 22 | 22 | eye_face.f00_gyul_op | 22 | eye_face.f00_gyul_op |
| 0 | cf_O_face | 23 | 23 | eye_face.f00_gyur_op | 23 | eye_face.f00_gyur_op |
| 0 | cf_O_face | 24 | 24 | eye_face.f00_gyu_cl | 24 | eye_face.f00_gyu_cl |
| 0 | cf_O_face | 25 | 25 | eye_face.f00_gyul02_op | 25 | eye_face.f00_gyul02_op |
| 0 | cf_O_face | 26 | 26 | eye_face.f00_gyur02_op | 26 | eye_face.f00_gyur02_op |
| 0 | cf_O_face | 27 | 27 | eye_face.f00_gyu02_cl | 27 | eye_face.f00_gyu02_cl |
| 0 | cf_O_face | 28 | 2 | eye_face.f00_egao_cl | 5 | eye_face.f00_winkl_op |
| 0 | cf_O_face | 29 | 2 | eye_face.f00_egao_cl | 6 | eye_face.f00_winkr_op |
| 1 | cf_O_noseline | 0 | 0 | eye_nose.nl00_def_cl | 1 | eye_nose.nl00_def_op |
| 1 | cf_O_noseline | 1 | 0 | eye_nose.nl00_def_cl | 0 | eye_nose.nl00_def_cl |
| 1 | cf_O_noseline | 2 | 2 | eye_nose.nl00_egao_cl | 1 | eye_nose.nl00_def_op |
| 1 | cf_O_noseline | 3 | 2 | eye_nose.nl00_egao_cl | 2 | eye_nose.nl00_egao_cl |
| 1 | cf_O_noseline | 4 | 2 | eye_nose.nl00_egao_cl | 4 | eye_nose.nl00_bisyou_op |
| 1 | cf_O_noseline | 5 | 5 | eye_nose.nl00_winkl_op | 5 | eye_nose.nl00_winkl_op |
| 1 | cf_O_noseline | 6 | 6 | eye_nose.nl00_winkr_op | 6 | eye_nose.nl00_winkr_op |
| 1 | cf_O_noseline | 7 | 0 | eye_nose.nl00_def_cl | 7 | eye_nose.nl00_setunai_op |
| 1 | cf_O_noseline | 8 | 0 | eye_nose.nl00_def_cl | 8 | eye_nose.nl00_tere_op |
| 1 | cf_O_noseline | 9 | 0 | eye_nose.nl00_def_cl | 9 | eye_nose.nl00_ikari_op |
| 1 | cf_O_noseline | 10 | 0 | eye_nose.nl00_def_cl | 10 | eye_nose.nl00_sinken_op |
| 1 | cf_O_noseline | 11 | 0 | eye_nose.nl00_def_cl | 11 | eye_nose.nl00_tumara_op |
| 1 | cf_O_noseline | 12 | 0 | eye_nose.nl00_def_cl | 12 | eye_nose.nl00_kurusi_op |
| 1 | cf_O_noseline | 13 | 0 | eye_nose.nl00_def_cl | 13 | eye_nose.nl00_keno_op |
| 1 | cf_O_noseline | 14 | 0 | eye_nose.nl00_def_cl | 14 | eye_nose.nl00_sian_op |
| 1 | cf_O_noseline | 15 | 0 | eye_nose.nl00_def_cl | 15 | eye_nose.nl00_kanasi_op |
| 1 | cf_O_noseline | 16 | 0 | eye_nose.nl00_def_cl | 16 | eye_nose.nl00_naki_op |
| 1 | cf_O_noseline | 17 | 0 | eye_nose.nl00_def_cl | 17 | eye_nose.nl00_aseri_op |
| 1 | cf_O_noseline | 18 | 0 | eye_nose.nl00_def_cl | 18 | eye_nose.nl00_rakutan_op |
| 1 | cf_O_noseline | 19 | 0 | eye_nose.nl00_def_cl | 19 | eye_nose.nl00_komaru_op |
| 1 | cf_O_noseline | 20 | 0 | eye_nose.nl00_def_cl | 20 | eye_nose.nl00_doya_op |
| 1 | cf_O_noseline | 21 | 21 | eye_nose.nl00_gag | 21 | eye_nose.nl00_gag |
| 1 | cf_O_noseline | 22 | 22 | eye_nose.nl00_gyul_op | 22 | eye_nose.nl00_gyul_op |
| 1 | cf_O_noseline | 23 | 23 | eye_nose.nl00_gyur_op | 23 | eye_nose.nl00_gyur_op |
| 1 | cf_O_noseline | 24 | 24 | eye_nose.nl00_gyu_cl | 24 | eye_nose.nl00_gyu_cl |
| 1 | cf_O_noseline | 25 | 22 | eye_nose.nl00_gyul_op | 22 | eye_nose.nl00_gyul_op |
| 1 | cf_O_noseline | 26 | 23 | eye_nose.nl00_gyur_op | 23 | eye_nose.nl00_gyur_op |
| 1 | cf_O_noseline | 27 | 24 | eye_nose.nl00_gyu_cl | 24 | eye_nose.nl00_gyu_cl |
| 1 | cf_O_noseline | 28 | 2 | eye_nose.nl00_egao_cl | 5 | eye_nose.nl00_winkl_op |
| 1 | cf_O_noseline | 29 | 2 | eye_nose.nl00_egao_cl | 6 | eye_nose.nl00_winkr_op |
| 2 | cf_Ohitomi_L | 0 | 0 | eye_siroL.sL00_def_cl | 1 | eye_siroL.sL00_def_op |
| 2 | cf_Ohitomi_L | 1 | 0 | eye_siroL.sL00_def_cl | 0 | eye_siroL.sL00_def_cl |
| 2 | cf_Ohitomi_L | 2 | 2 | eye_siroL.sL00_egao_cl | 3 | eye_siroL.sL00_egao_op |
| 2 | cf_Ohitomi_L | 3 | 2 | eye_siroL.sL00_egao_cl | 2 | eye_siroL.sL00_egao_cl |
| 2 | cf_Ohitomi_L | 4 | 2 | eye_siroL.sL00_egao_cl | 4 | eye_siroL.sL00_bisyou_op |
| 2 | cf_Ohitomi_L | 5 | 5 | eye_siroL.sL00_winkl_op | 5 | eye_siroL.sL00_winkl_op |
| 2 | cf_Ohitomi_L | 6 | 6 | eye_siroL.sL00_winkr_op | 6 | eye_siroL.sL00_winkr_op |
| 2 | cf_Ohitomi_L | 7 | 0 | eye_siroL.sL00_def_cl | 7 | eye_siroL.sL00_setunai_op |
| 2 | cf_Ohitomi_L | 8 | 0 | eye_siroL.sL00_def_cl | 8 | eye_siroL.sL00_tere_op |
| 2 | cf_Ohitomi_L | 9 | 0 | eye_siroL.sL00_def_cl | 9 | eye_siroL.sL00_ikari_op |
| 2 | cf_Ohitomi_L | 10 | 0 | eye_siroL.sL00_def_cl | 10 | eye_siroL.sL00_sinken_op |
| 2 | cf_Ohitomi_L | 11 | 0 | eye_siroL.sL00_def_cl | 11 | eye_siroL.sL00_tumara_op |
| 2 | cf_Ohitomi_L | 12 | 0 | eye_siroL.sL00_def_cl | 12 | eye_siroL.sL00_kurusi_op |
| 2 | cf_Ohitomi_L | 13 | 0 | eye_siroL.sL00_def_cl | 13 | eye_siroL.sL00_keno_op |
| 2 | cf_Ohitomi_L | 14 | 0 | eye_siroL.sL00_def_cl | 14 | eye_siroL.sL00_sian_op |
| 2 | cf_Ohitomi_L | 15 | 0 | eye_siroL.sL00_def_cl | 15 | eye_siroL.sL00_kanasi_op |
| 2 | cf_Ohitomi_L | 16 | 0 | eye_siroL.sL00_def_cl | 16 | eye_siroL.sL00_naki_op |
| 2 | cf_Ohitomi_L | 17 | 0 | eye_siroL.sL00_def_cl | 17 | eye_siroL.sL00_aseri_op |
| 2 | cf_Ohitomi_L | 18 | 0 | eye_siroL.sL00_def_cl | 18 | eye_siroL.sL00_rakutan_op |
| 2 | cf_Ohitomi_L | 19 | 0 | eye_siroL.sL00_def_cl | 19 | eye_siroL.sL00_komaru_op |
| 2 | cf_Ohitomi_L | 20 | 0 | eye_siroL.sL00_def_cl | 20 | eye_siroL.sL00_doya_op |
| 2 | cf_Ohitomi_L | 21 | 0 | eye_siroL.sL00_def_cl | 1 | eye_siroL.sL00_def_op |
| 2 | cf_Ohitomi_L | 22 | 21 | eye_siroL.sL00_gyul_op | 21 | eye_siroL.sL00_gyul_op |
| 2 | cf_Ohitomi_L | 23 | 22 | eye_siroL.sL00_gyur_op | 22 | eye_siroL.sL00_gyur_op |
| 2 | cf_Ohitomi_L | 24 | 23 | eye_siroL.sL00_gyu_cl | 23 | eye_siroL.sL00_gyu_cl |
| 2 | cf_Ohitomi_L | 25 | 24 | eye_siroL.sL00_gyul02_op | 24 | eye_siroL.sL00_gyul02_op |
| 2 | cf_Ohitomi_L | 26 | 25 | eye_siroL.sL00_gyur02_op | 25 | eye_siroL.sL00_gyur02_op |
| 2 | cf_Ohitomi_L | 27 | 26 | eye_siroL.sL00_gyu02_cl | 26 | eye_siroL.sL00_gyu02_cl |
| 2 | cf_Ohitomi_L | 28 | 2 | eye_siroL.sL00_egao_cl | 5 | eye_siroL.sL00_winkl_op |
| 2 | cf_Ohitomi_L | 29 | 2 | eye_siroL.sL00_egao_cl | 6 | eye_siroL.sL00_winkr_op |
| 3 | cf_Ohitomi_R | 0 | 0 | eye_siroR.sR00_def_cl | 1 | eye_siroR.sR00_def_op |
| 3 | cf_Ohitomi_R | 1 | 0 | eye_siroR.sR00_def_cl | 0 | eye_siroR.sR00_def_cl |
| 3 | cf_Ohitomi_R | 2 | 2 | eye_siroR.sR00_egao_cl | 3 | eye_siroR.sR00_egao_op |
| 3 | cf_Ohitomi_R | 3 | 2 | eye_siroR.sR00_egao_cl | 2 | eye_siroR.sR00_egao_cl |
| 3 | cf_Ohitomi_R | 4 | 2 | eye_siroR.sR00_egao_cl | 4 | eye_siroR.sR00_bisyou_op |
| 3 | cf_Ohitomi_R | 5 | 5 | eye_siroR.sR00_winkl_op | 5 | eye_siroR.sR00_winkl_op |
| 3 | cf_Ohitomi_R | 6 | 6 | eye_siroR.sR00_winkr_op | 6 | eye_siroR.sR00_winkr_op |
| 3 | cf_Ohitomi_R | 7 | 0 | eye_siroR.sR00_def_cl | 7 | eye_siroR.sR00_setunai_op |
| 3 | cf_Ohitomi_R | 8 | 0 | eye_siroR.sR00_def_cl | 8 | eye_siroR.sR00_tere_op |
| 3 | cf_Ohitomi_R | 9 | 0 | eye_siroR.sR00_def_cl | 9 | eye_siroR.sR00_ikari_op |
| 3 | cf_Ohitomi_R | 10 | 0 | eye_siroR.sR00_def_cl | 10 | eye_siroR.sR00_sinken_op |
| 3 | cf_Ohitomi_R | 11 | 0 | eye_siroR.sR00_def_cl | 11 | eye_siroR.sR00_tumara_op |
| 3 | cf_Ohitomi_R | 12 | 0 | eye_siroR.sR00_def_cl | 12 | eye_siroR.sR00_kurusi_op |
| 3 | cf_Ohitomi_R | 13 | 0 | eye_siroR.sR00_def_cl | 13 | eye_siroR.sR00_keno_op |
| 3 | cf_Ohitomi_R | 14 | 0 | eye_siroR.sR00_def_cl | 14 | eye_siroR.sR00_sian_op |
| 3 | cf_Ohitomi_R | 15 | 0 | eye_siroR.sR00_def_cl | 15 | eye_siroR.sR00_kanasi_op |
| 3 | cf_Ohitomi_R | 16 | 0 | eye_siroR.sR00_def_cl | 16 | eye_siroR.sR00_naki_op |
| 3 | cf_Ohitomi_R | 17 | 0 | eye_siroR.sR00_def_cl | 17 | eye_siroR.sR00_aseri_op |
| 3 | cf_Ohitomi_R | 18 | 0 | eye_siroR.sR00_def_cl | 18 | eye_siroR.sR00_rakutan_op |
| 3 | cf_Ohitomi_R | 19 | 0 | eye_siroR.sR00_def_cl | 19 | eye_siroR.sR00_komaru_op |
| 3 | cf_Ohitomi_R | 20 | 0 | eye_siroR.sR00_def_cl | 20 | eye_siroR.sR00_doya_op |
| 3 | cf_Ohitomi_R | 21 | 0 | eye_siroR.sR00_def_cl | 1 | eye_siroR.sR00_def_op |
| 3 | cf_Ohitomi_R | 22 | 21 | eye_siroR.sR00_gyul_op | 21 | eye_siroR.sR00_gyul_op |
| 3 | cf_Ohitomi_R | 23 | 22 | eye_siroR.sR00_gyur_op | 22 | eye_siroR.sR00_gyur_op |
| 3 | cf_Ohitomi_R | 24 | 23 | eye_siroR.sR00_gyu_cl | 23 | eye_siroR.sR00_gyu_cl |
| 3 | cf_Ohitomi_R | 25 | 24 | eye_siroR.sR00_gyul02_op | 24 | eye_siroR.sR00_gyul02_op |
| 3 | cf_Ohitomi_R | 26 | 25 | eye_siroR.sR00_gyur02_op | 25 | eye_siroR.sR00_gyur02_op |
| 3 | cf_Ohitomi_R | 27 | 26 | eye_siroR.sR00_gyu02_cl | 26 | eye_siroR.sR00_gyu02_cl |
| 3 | cf_Ohitomi_R | 28 | 2 | eye_siroR.sR00_egao_cl | 5 | eye_siroR.sR00_winkl_op |
| 3 | cf_Ohitomi_R | 29 | 2 | eye_siroR.sR00_egao_cl | 6 | eye_siroR.sR00_winkr_op |
| 4 | cf_O_eyeline | 0 | 0 | eye_line_u.elu00_def_cl | 1 | eye_line_u.elu00_def_op |
| 4 | cf_O_eyeline | 1 | 0 | eye_line_u.elu00_def_cl | 0 | eye_line_u.elu00_def_cl |
| 4 | cf_O_eyeline | 2 | 2 | eye_line_u.elu00_egao_cl | 3 | eye_line_u.elu00_egao_op |
| 4 | cf_O_eyeline | 3 | 2 | eye_line_u.elu00_egao_cl | 2 | eye_line_u.elu00_egao_cl |
| 4 | cf_O_eyeline | 4 | 2 | eye_line_u.elu00_egao_cl | 4 | eye_line_u.elu00_bisyou_op |
| 4 | cf_O_eyeline | 5 | 5 | eye_line_u.elu00_winkl_op | 5 | eye_line_u.elu00_winkl_op |
| 4 | cf_O_eyeline | 6 | 6 | eye_line_u.elu00_winkr_op | 6 | eye_line_u.elu00_winkr_op |
| 4 | cf_O_eyeline | 7 | 0 | eye_line_u.elu00_def_cl | 7 | eye_line_u.elu00_setunai_op |
| 4 | cf_O_eyeline | 8 | 0 | eye_line_u.elu00_def_cl | 8 | eye_line_u.elu00_tere_op |
| 4 | cf_O_eyeline | 9 | 0 | eye_line_u.elu00_def_cl | 9 | eye_line_u.elu00_ikari_op |
| 4 | cf_O_eyeline | 10 | 0 | eye_line_u.elu00_def_cl | 10 | eye_line_u.elu00_sinken_op |
| 4 | cf_O_eyeline | 11 | 0 | eye_line_u.elu00_def_cl | 11 | eye_line_u.elu00_tumara_op |
| 4 | cf_O_eyeline | 12 | 0 | eye_line_u.elu00_def_cl | 12 | eye_line_u.elu00_kurusi_op |
| 4 | cf_O_eyeline | 13 | 0 | eye_line_u.elu00_def_cl | 13 | eye_line_u.elu00_keno_op |
| 4 | cf_O_eyeline | 14 | 0 | eye_line_u.elu00_def_cl | 14 | eye_line_u.elu00_sian_op |
| 4 | cf_O_eyeline | 15 | 0 | eye_line_u.elu00_def_cl | 15 | eye_line_u.elu00_kanasi_op |
| 4 | cf_O_eyeline | 16 | 0 | eye_line_u.elu00_def_cl | 16 | eye_line_u.elu00_naki_op |
| 4 | cf_O_eyeline | 17 | 0 | eye_line_u.elu00_def_cl | 17 | eye_line_u.elu00_aseri_op |
| 4 | cf_O_eyeline | 18 | 0 | eye_line_u.elu00_def_cl | 18 | eye_line_u.elu00_rakutan_op |
| 4 | cf_O_eyeline | 19 | 0 | eye_line_u.elu00_def_cl | 19 | eye_line_u.elu00_komaru_op |
| 4 | cf_O_eyeline | 20 | 0 | eye_line_u.elu00_def_cl | 20 | eye_line_u.elu00_doya_op |
| 4 | cf_O_eyeline | 21 | 0 | eye_line_u.elu00_def_cl | 1 | eye_line_u.elu00_def_op |
| 4 | cf_O_eyeline | 22 | 21 | eye_line_u.elu00_gyul_op | 21 | eye_line_u.elu00_gyul_op |
| 4 | cf_O_eyeline | 23 | 22 | eye_line_u.elu00_gyur_op | 22 | eye_line_u.elu00_gyur_op |
| 4 | cf_O_eyeline | 24 | 23 | eye_line_u.elu00_gyu_cl | 23 | eye_line_u.elu00_gyu_cl |
| 4 | cf_O_eyeline | 25 | 24 | eye_line_u.elu00_gyul02_op | 24 | eye_line_u.elu00_gyul02_op |
| 4 | cf_O_eyeline | 26 | 25 | eye_line_u.elu00_gyur02_op | 25 | eye_line_u.elu00_gyur02_op |
| 4 | cf_O_eyeline | 27 | 26 | eye_line_u.elu00_gyu02_cl | 26 | eye_line_u.elu00_gyu02_cl |
| 4 | cf_O_eyeline | 28 | 2 | eye_line_u.elu00_egao_cl | 5 | eye_line_u.elu00_winkl_op |
| 4 | cf_O_eyeline | 29 | 2 | eye_line_u.elu00_egao_cl | 6 | eye_line_u.elu00_winkr_op |
| 5 | cf_O_eyeline_low | 0 | 0 | eye_line_l.ell00_def_cl | 1 | eye_line_l.ell00_def_op |
| 5 | cf_O_eyeline_low | 1 | 0 | eye_line_l.ell00_def_cl | 0 | eye_line_l.ell00_def_cl |
| 5 | cf_O_eyeline_low | 2 | 2 | eye_line_l.ell00_egao_cl | 3 | eye_line_l.ell00_egao_op |
| 5 | cf_O_eyeline_low | 3 | 2 | eye_line_l.ell00_egao_cl | 2 | eye_line_l.ell00_egao_cl |
| 5 | cf_O_eyeline_low | 4 | 2 | eye_line_l.ell00_egao_cl | 4 | eye_line_l.ell00_bisyou_op |
| 5 | cf_O_eyeline_low | 5 | 5 | eye_line_l.ell00_winkl_op | 5 | eye_line_l.ell00_winkl_op |
| 5 | cf_O_eyeline_low | 6 | 6 | eye_line_l.ell00_winkr_op | 6 | eye_line_l.ell00_winkr_op |
| 5 | cf_O_eyeline_low | 7 | 0 | eye_line_l.ell00_def_cl | 7 | eye_line_l.ell00_setunai_op |
| 5 | cf_O_eyeline_low | 8 | 0 | eye_line_l.ell00_def_cl | 8 | eye_line_l.ell00_tere_op |
| 5 | cf_O_eyeline_low | 9 | 0 | eye_line_l.ell00_def_cl | 9 | eye_line_l.ell00_ikari_op |
| 5 | cf_O_eyeline_low | 10 | 0 | eye_line_l.ell00_def_cl | 10 | eye_line_l.ell00_sinken_op |
| 5 | cf_O_eyeline_low | 11 | 0 | eye_line_l.ell00_def_cl | 11 | eye_line_l.ell00_tumara_op |
| 5 | cf_O_eyeline_low | 12 | 0 | eye_line_l.ell00_def_cl | 12 | eye_line_l.ell00_kurusi_op |
| 5 | cf_O_eyeline_low | 13 | 0 | eye_line_l.ell00_def_cl | 13 | eye_line_l.ell00_keno_op |
| 5 | cf_O_eyeline_low | 14 | 0 | eye_line_l.ell00_def_cl | 14 | eye_line_l.ell00_sian_op |
| 5 | cf_O_eyeline_low | 15 | 0 | eye_line_l.ell00_def_cl | 15 | eye_line_l.ell00_kanasi_op |
| 5 | cf_O_eyeline_low | 16 | 0 | eye_line_l.ell00_def_cl | 16 | eye_line_l.ell00_naki_op |
| 5 | cf_O_eyeline_low | 17 | 0 | eye_line_l.ell00_def_cl | 17 | eye_line_l.ell00_aseri_op |
| 5 | cf_O_eyeline_low | 18 | 0 | eye_line_l.ell00_def_cl | 18 | eye_line_l.ell00_rakutan_op |
| 5 | cf_O_eyeline_low | 19 | 0 | eye_line_l.ell00_def_cl | 19 | eye_line_l.ell00_komaru_op |
| 5 | cf_O_eyeline_low | 20 | 0 | eye_line_l.ell00_def_cl | 20 | eye_line_l.ell00_doya_op |
| 5 | cf_O_eyeline_low | 21 | 0 | eye_line_l.ell00_def_cl | 1 | eye_line_l.ell00_def_op |
| 5 | cf_O_eyeline_low | 22 | 21 | eye_line_l.ell00_gyul_op | 21 | eye_line_l.ell00_gyul_op |
| 5 | cf_O_eyeline_low | 23 | 22 | eye_line_l.ell00_gyur_op | 22 | eye_line_l.ell00_gyur_op |
| 5 | cf_O_eyeline_low | 24 | 23 | eye_line_l.ell00_gyu_cl | 23 | eye_line_l.ell00_gyu_cl |
| 5 | cf_O_eyeline_low | 25 | 24 | eye_line_l.ell00_gyul02_op | 24 | eye_line_l.ell00_gyul02_op |
| 5 | cf_O_eyeline_low | 26 | 25 | eye_line_l.ell00_gyur02_op | 25 | eye_line_l.ell00_gyur02_op |
| 5 | cf_O_eyeline_low | 27 | 26 | eye_line_l.ell00_gyu02_cl | 26 | eye_line_l.ell00_gyu02_cl |
| 5 | cf_O_eyeline_low | 28 | 2 | eye_line_l.ell00_egao_cl | 5 | eye_line_l.ell00_winkl_op |
| 5 | cf_O_eyeline_low | 29 | 2 | eye_line_l.ell00_egao_cl | 6 | eye_line_l.ell00_winkr_op |
| 6 | cf_O_namida_L | 0 | 0 | eye_naL.naL00_def_cl | 1 | eye_naL.naL00_def_op |
| 6 | cf_O_namida_L | 1 | 0 | eye_naL.naL00_def_cl | 0 | eye_naL.naL00_def_cl |
| 6 | cf_O_namida_L | 2 | 2 | eye_naL.naL00_egao_cl | 3 | eye_naL.naL00_egao_op |
| 6 | cf_O_namida_L | 3 | 2 | eye_naL.naL00_egao_cl | 2 | eye_naL.naL00_egao_cl |
| 6 | cf_O_namida_L | 4 | 2 | eye_naL.naL00_egao_cl | 4 | eye_naL.naL00_bisyou_op |
| 6 | cf_O_namida_L | 5 | 5 | eye_naL.naL00_winkl_op | 5 | eye_naL.naL00_winkl_op |
| 6 | cf_O_namida_L | 6 | 6 | eye_naL.naL00_winkr_op | 6 | eye_naL.naL00_winkr_op |
| 6 | cf_O_namida_L | 7 | 0 | eye_naL.naL00_def_cl | 7 | eye_naL.naL00_setunai_op |
| 6 | cf_O_namida_L | 8 | 0 | eye_naL.naL00_def_cl | 8 | eye_naL.naL00_tere_op |
| 6 | cf_O_namida_L | 9 | 0 | eye_naL.naL00_def_cl | 9 | eye_naL.naL00_ikari_op |
| 6 | cf_O_namida_L | 10 | 0 | eye_naL.naL00_def_cl | 10 | eye_naL.naL00_sinken_op |
| 6 | cf_O_namida_L | 11 | 0 | eye_naL.naL00_def_cl | 11 | eye_naL.naL00_tumara_op |
| 6 | cf_O_namida_L | 12 | 0 | eye_naL.naL00_def_cl | 12 | eye_naL.naL00_kurusi_op |
| 6 | cf_O_namida_L | 13 | 0 | eye_naL.naL00_def_cl | 13 | eye_naL.naL00_keno_op |
| 6 | cf_O_namida_L | 14 | 0 | eye_naL.naL00_def_cl | 14 | eye_naL.naL00_sian_op |
| 6 | cf_O_namida_L | 15 | 0 | eye_naL.naL00_def_cl | 15 | eye_naL.naL00_kanasi_op |
| 6 | cf_O_namida_L | 16 | 0 | eye_naL.naL00_def_cl | 16 | eye_naL.naL00_naki_op |
| 6 | cf_O_namida_L | 17 | 0 | eye_naL.naL00_def_cl | 17 | eye_naL.naL00_aseri_op |
| 6 | cf_O_namida_L | 18 | 0 | eye_naL.naL00_def_cl | 18 | eye_naL.naL00_rakutan_op |
| 6 | cf_O_namida_L | 19 | 0 | eye_naL.naL00_def_cl | 19 | eye_naL.naL00_komaru_op |
| 6 | cf_O_namida_L | 20 | 0 | eye_naL.naL00_def_cl | 20 | eye_naL.naL00_doya_op |
| 6 | cf_O_namida_L | 21 | 0 | eye_naL.naL00_def_cl | 1 | eye_naL.naL00_def_op |
| 6 | cf_O_namida_L | 22 | 21 | eye_naL.naL00_gyul_op | 21 | eye_naL.naL00_gyul_op |
| 6 | cf_O_namida_L | 23 | 22 | eye_naL.naL00_gyur_op | 22 | eye_naL.naL00_gyur_op |
| 6 | cf_O_namida_L | 24 | 23 | eye_naL.naL00_gyu_cl | 23 | eye_naL.naL00_gyu_cl |
| 6 | cf_O_namida_L | 25 | 24 | eye_naL.naL00_gyul02_op | 24 | eye_naL.naL00_gyul02_op |
| 6 | cf_O_namida_L | 26 | 25 | eye_naL.naL00_gyur02_op | 25 | eye_naL.naL00_gyur02_op |
| 6 | cf_O_namida_L | 27 | 26 | eye_naL.naL00_gyu02_cl | 26 | eye_naL.naL00_gyu02_cl |
| 6 | cf_O_namida_L | 28 | 2 | eye_naL.naL00_egao_cl | 5 | eye_naL.naL00_winkl_op |
| 6 | cf_O_namida_L | 29 | 2 | eye_naL.naL00_egao_cl | 6 | eye_naL.naL00_winkr_op |
| 7 | cf_O_namida_M | 0 | 0 | eye_naM.naM00_def_cl | 1 | eye_naM.naM00_def_op |
| 7 | cf_O_namida_M | 1 | 0 | eye_naM.naM00_def_cl | 0 | eye_naM.naM00_def_cl |
| 7 | cf_O_namida_M | 2 | 2 | eye_naM.naM00_egao_cl | 3 | eye_naM.naM00_egao_op |
| 7 | cf_O_namida_M | 3 | 2 | eye_naM.naM00_egao_cl | 2 | eye_naM.naM00_egao_cl |
| 7 | cf_O_namida_M | 4 | 2 | eye_naM.naM00_egao_cl | 4 | eye_naM.naM00_bisyou_op |
| 7 | cf_O_namida_M | 5 | 5 | eye_naM.naM00_winkl_op | 5 | eye_naM.naM00_winkl_op |
| 7 | cf_O_namida_M | 6 | 6 | eye_naM.naM00_winkr_op | 6 | eye_naM.naM00_winkr_op |
| 7 | cf_O_namida_M | 7 | 0 | eye_naM.naM00_def_cl | 7 | eye_naM.naM00_setunai_op |
| 7 | cf_O_namida_M | 8 | 0 | eye_naM.naM00_def_cl | 8 | eye_naM.naM00_tere_op |
| 7 | cf_O_namida_M | 9 | 0 | eye_naM.naM00_def_cl | 9 | eye_naM.naM00_ikari_op |
| 7 | cf_O_namida_M | 10 | 0 | eye_naM.naM00_def_cl | 10 | eye_naM.naM00_sinken_op |
| 7 | cf_O_namida_M | 11 | 0 | eye_naM.naM00_def_cl | 11 | eye_naM.naM00_tumara_op |
| 7 | cf_O_namida_M | 12 | 0 | eye_naM.naM00_def_cl | 12 | eye_naM.naM00_kurusi_op |
| 7 | cf_O_namida_M | 13 | 0 | eye_naM.naM00_def_cl | 13 | eye_naM.naM00_keno_op |
| 7 | cf_O_namida_M | 14 | 0 | eye_naM.naM00_def_cl | 14 | eye_naM.naM00_sian_op |
| 7 | cf_O_namida_M | 15 | 0 | eye_naM.naM00_def_cl | 15 | eye_naM.naM00_kanasi_op |
| 7 | cf_O_namida_M | 16 | 0 | eye_naM.naM00_def_cl | 16 | eye_naM.naM00_naki_op |
| 7 | cf_O_namida_M | 17 | 0 | eye_naM.naM00_def_cl | 17 | eye_naM.naM00_aseri_op |
| 7 | cf_O_namida_M | 18 | 0 | eye_naM.naM00_def_cl | 18 | eye_naM.naM00_rakutan_op |
| 7 | cf_O_namida_M | 19 | 0 | eye_naM.naM00_def_cl | 19 | eye_naM.naM00_komaru_op |
| 7 | cf_O_namida_M | 20 | 0 | eye_naM.naM00_def_cl | 20 | eye_naM.naM00_doya_op |
| 7 | cf_O_namida_M | 21 | 0 | eye_naM.naM00_def_cl | 1 | eye_naM.naM00_def_op |
| 7 | cf_O_namida_M | 22 | 21 | eye_naM.naM00_gyul_op | 21 | eye_naM.naM00_gyul_op |
| 7 | cf_O_namida_M | 23 | 22 | eye_naM.naM00_gyur_op | 22 | eye_naM.naM00_gyur_op |
| 7 | cf_O_namida_M | 24 | 23 | eye_naM.naM00_gyu_cl | 23 | eye_naM.naM00_gyu_cl |
| 7 | cf_O_namida_M | 25 | 24 | eye_naM.naM00_gyul02_op | 24 | eye_naM.naM00_gyul02_op |
| 7 | cf_O_namida_M | 26 | 25 | eye_naM.naM00_gyur02_op | 25 | eye_naM.naM00_gyur02_op |
| 7 | cf_O_namida_M | 27 | 26 | eye_naM.naM00_gyu02_cl | 26 | eye_naM.naM00_gyu02_cl |
| 7 | cf_O_namida_M | 28 | 2 | eye_naM.naM00_egao_cl | 5 | eye_naM.naM00_winkl_op |
| 7 | cf_O_namida_M | 29 | 2 | eye_naM.naM00_egao_cl | 6 | eye_naM.naM00_winkr_op |
| 8 | cf_O_namida_S | 0 | 0 | eye_naS.naS00_def_cl | 1 | eye_naS.naS00_def_op |
| 8 | cf_O_namida_S | 1 | 0 | eye_naS.naS00_def_cl | 0 | eye_naS.naS00_def_cl |
| 8 | cf_O_namida_S | 2 | 2 | eye_naS.naS00_egao_cl | 3 | eye_naS.naS00_egao_op |
| 8 | cf_O_namida_S | 3 | 2 | eye_naS.naS00_egao_cl | 2 | eye_naS.naS00_egao_cl |
| 8 | cf_O_namida_S | 4 | 2 | eye_naS.naS00_egao_cl | 4 | eye_naS.naS00_bisyou_op |
| 8 | cf_O_namida_S | 5 | 5 | eye_naS.naS00_winkl_op | 5 | eye_naS.naS00_winkl_op |
| 8 | cf_O_namida_S | 6 | 6 | eye_naS.naS00_winkr_op | 6 | eye_naS.naS00_winkr_op |
| 8 | cf_O_namida_S | 7 | 0 | eye_naS.naS00_def_cl | 7 | eye_naS.naS00_setunai_op |
| 8 | cf_O_namida_S | 8 | 0 | eye_naS.naS00_def_cl | 8 | eye_naS.naS00_tere_op |
| 8 | cf_O_namida_S | 9 | 0 | eye_naS.naS00_def_cl | 9 | eye_naS.naS00_ikari_op |
| 8 | cf_O_namida_S | 10 | 0 | eye_naS.naS00_def_cl | 10 | eye_naS.naS00_sinken_op |
| 8 | cf_O_namida_S | 11 | 0 | eye_naS.naS00_def_cl | 11 | eye_naS.naS00_tumara_op |
| 8 | cf_O_namida_S | 12 | 0 | eye_naS.naS00_def_cl | 12 | eye_naS.naS00_kurusi_op |
| 8 | cf_O_namida_S | 13 | 0 | eye_naS.naS00_def_cl | 13 | eye_naS.naS00_keno_op |
| 8 | cf_O_namida_S | 14 | 0 | eye_naS.naS00_def_cl | 14 | eye_naS.naS00_sian_op |
| 8 | cf_O_namida_S | 15 | 0 | eye_naS.naS00_def_cl | 15 | eye_naS.naS00_kanasi_op |
| 8 | cf_O_namida_S | 16 | 0 | eye_naS.naS00_def_cl | 16 | eye_naS.naS00_naki_op |
| 8 | cf_O_namida_S | 17 | 0 | eye_naS.naS00_def_cl | 17 | eye_naS.naS00_aseri_op |
| 8 | cf_O_namida_S | 18 | 0 | eye_naS.naS00_def_cl | 18 | eye_naS.naS00_rakutan_op |
| 8 | cf_O_namida_S | 19 | 0 | eye_naS.naS00_def_cl | 19 | eye_naS.naS00_komaru_op |
| 8 | cf_O_namida_S | 20 | 0 | eye_naS.naS00_def_cl | 20 | eye_naS.naS00_doya_op |
| 8 | cf_O_namida_S | 21 | 0 | eye_naS.naS00_def_cl | 1 | eye_naS.naS00_def_op |
| 8 | cf_O_namida_S | 22 | 21 | eye_naS.naS00_gyul_op | 21 | eye_naS.naS00_gyul_op |
| 8 | cf_O_namida_S | 23 | 22 | eye_naS.naS00_gyur_op | 22 | eye_naS.naS00_gyur_op |
| 8 | cf_O_namida_S | 24 | 23 | eye_naS.naS00_gyu_cl | 23 | eye_naS.naS00_gyu_cl |
| 8 | cf_O_namida_S | 25 | 24 | eye_naS.naS00_gyul02_op | 24 | eye_naS.naS00_gyul02_op |
| 8 | cf_O_namida_S | 26 | 25 | eye_naS.naS00_gyur02_op | 25 | eye_naS.naS00_gyur02_op |
| 8 | cf_O_namida_S | 27 | 26 | eye_naS.naS00_gyu02_cl | 26 | eye_naS.naS00_gyu02_cl |
| 8 | cf_O_namida_S | 28 | 2 | eye_naS.naS00_egao_cl | 5 | eye_naS.naS00_winkl_op |
| 8 | cf_O_namida_S | 29 | 2 | eye_naS.naS00_egao_cl | 6 | eye_naS.naS00_winkr_op |

#### EyebrowCtrl

| target | mesh | ptn | close | close_name | open | open_name |
|---|---|---|---|---|---|---|
| 0 | cf_O_mayuge | 0 | 0 | mayuge.mayu00_def_cl | 1 | mayuge.mayu00_def_op |
| 0 | cf_O_mayuge | 1 | 2 | mayuge.mayu00_oko_cl | 3 | mayuge.mayu00_oko_op |
| 0 | cf_O_mayuge | 2 | 4 | mayuge.mayu00_koma_cl | 5 | mayuge.mayu00_koma_op |
| 0 | cf_O_mayuge | 3 | 6 | mayuge.mayu00_tmara_cl | 7 | mayuge.mayu00_tmara_op |
| 0 | cf_O_mayuge | 4 | 8 | mayuge.mayu00_gimoL_cl | 9 | mayuge.mayu00_gimoL_op |
| 0 | cf_O_mayuge | 5 | 10 | mayuge.mayu00_gimoR_cl | 11 | mayuge.mayu00_gimoR_op |
| 0 | cf_O_mayuge | 6 | 12 | mayuge.mayu00_sianL_cl | 13 | mayuge.mayu00_sianL_op |
| 0 | cf_O_mayuge | 7 | 14 | mayuge.mayu00_sianR_cl | 15 | mayuge.mayu00_sianR_op |
| 0 | cf_O_mayuge | 8 | 16 | mayuge.mayu00_oko2L_cl | 17 | mayuge.mayu00_oko2L_op |
| 0 | cf_O_mayuge | 9 | 18 | mayuge.mayu00_oko2R_cl | 19 | mayuge.mayu00_oko2R_op |
| 0 | cf_O_mayuge | 10 | 20 | mayuge.mayu00_sinken_cl | 21 | mayuge.mayu00_sinken_op |
| 0 | cf_O_mayuge | 11 | 22 | mayuge.mayu00_huan_cl | 23 | mayuge.mayu00_huan_op |
| 0 | cf_O_mayuge | 12 | 24 | mayuge.mayu00_odoro_cl | 25 | mayuge.mayu00_odoro_op |
| 0 | cf_O_mayuge | 13 | 26 | mayuge.mayu00_rakutan_cl | 27 | mayuge.mayu00_rakutan_op |
| 0 | cf_O_mayuge | 14 | 28 | mayuge.mayu00_doya_cl | 29 | mayuge.mayu00_doya_op |
| 0 | cf_O_mayuge | 15 | 30 | mayuge.mayu00_winkl_cl | 31 | mayuge.mayu00_winkl_op |
| 0 | cf_O_mayuge | 16 | 32 | mayuge.mayu00_winkr_cl | 33 | mayuge.mayu00_winkr_op |

### 3.4 mod 頭部 prefab 的嘴型

mod 覆寫或新增的頭部 prefab（完整內容見 `fbs_patterns.tsv`）：

| asset | prefab | controllers |
|---|---|---|
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | p_cf_head_01 | EyebrowCtrl, EyesCtrl, MouthCtrl |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | p_cf_head_00 | EyebrowCtrl, EyesCtrl, MouthCtrl |

### 3.5 blendshape channel 一覽

| asset | mesh | channels |
|---|---|---|
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | cf_O_canine | 39 |
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | cf_O_eyeline | 27 |
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | cf_O_eyeline_low | 27 |
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | cf_O_face | 162 |
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | cf_O_mayuge | 34 |
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | cf_O_namida_L | 27 |
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | cf_O_namida_M | 27 |
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | cf_O_namida_S | 27 |
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | cf_O_noseline | 146 |
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | cf_O_tooth | 39 |
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | cf_Ohitomi_L | 27 |
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | cf_Ohitomi_R | 27 |
| C:\portable\KoikatuSunshine\mods\KKSImportedMods\付费作者系列含mod2\游戏Cetol\mods\enimaroah_Twilek_Head_v1.0.zipmod!abdata/chara/bo_head_33.unity3d | o_tang | 36 |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | cf_O_canine | 39 |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | cf_O_eyeline | 27 |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | cf_O_eyeline_low | 27 |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | cf_O_face | 162 |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | cf_O_mayuge | 34 |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | cf_O_namida_L | 27 |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | cf_O_namida_M | 27 |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | cf_O_namida_S | 27 |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | cf_O_noseline | 146 |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | cf_O_tooth | 39 |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | cf_Ohitomi_L | 27 |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | cf_Ohitomi_R | 27 |
| C:\portable\KoikatuSunshine\mods\Sideloader Modpack - KKS_Fixes\[DeathWeasel][KKS] Face Fixes v2.0.zipmod!abdata/chara/bo_head_00.unity3d | o_tang | 36 |
| abdata/chara/bo_head_00.unity3d | cf_O_canine | 117 |
| abdata/chara/bo_head_00.unity3d | cf_O_eyeline | 81 |
| abdata/chara/bo_head_00.unity3d | cf_O_eyeline_low | 81 |
| abdata/chara/bo_head_00.unity3d | cf_O_face | 486 |
| abdata/chara/bo_head_00.unity3d | cf_O_mayuge | 104 |
| abdata/chara/bo_head_00.unity3d | cf_O_namida_L | 81 |
| abdata/chara/bo_head_00.unity3d | cf_O_namida_M | 81 |
| abdata/chara/bo_head_00.unity3d | cf_O_namida_S | 81 |
| abdata/chara/bo_head_00.unity3d | cf_O_noseline | 438 |
| abdata/chara/bo_head_00.unity3d | cf_O_tooth | 117 |
| abdata/chara/bo_head_00.unity3d | cf_Ohitomi_L | 81 |
| abdata/chara/bo_head_00.unity3d | cf_Ohitomi_R | 81 |
| abdata/chara/bo_head_00.unity3d | o_tang | 108 |

完整 channel 名稱見 `fbs_channels.tsv`。

## 4. 輸出檔案

| 檔案 | 內容 |
|---|---|
| `Saved/anim_export/animation_sources.tsv` | 每個動畫 bundle 的 clip / controller 數量 |
| `Saved/anim_export/animation_clips.tsv` | **所有** AnimationClip（長度、取樣率、曲線數、binding 數） |
| `Saved/anim_export/studio_catalog.tsv` | Studio Anime／HAnime／HandAnime／group／category 目錄 |
| `Saved/anim_export/fbs_patterns.tsv` | 所有 FBS 控制器的 PtnSet（close/open 索引與名稱） |
| `Saved/anim_export/fbs_channels.tsv` | 每個 FBS 目標 mesh 的 blendshape channel 名稱 |
| `Saved/anim_export/mod_anim_bundles.tsv` | mods 內含 AnimationClip 的 bundle 清單 |
| `Saved/anim_export/mod_studio_info.tsv` | mods 內 `studio/info` 目錄 CSV／bundle 清單 |
| `Saved/anim_export/mod_head_prefabs.tsv` | mods 內的 `chara/bo_head_*.unity3d` |
| `Tools/anim_export/scan_mods.lua` | 掃描 mods（動畫 bundle／studio/info／頭部 prefab） |
| `Tools/anim_export/export_anim_mouth.lua` | 產生全部 TSV |
| `Tools/anim_export/make_md.lua` | 產生本文件 |

## 5. 重現指令

```powershell
Tools\lua\lua.exe Tools/anim_export/scan_mods.lua            # 掃描 mods（只需重跑一次）
Tools\lua\lua.exe Tools/anim_export/export_anim_mouth.lua    # 產生全部 TSV
Tools\lua\lua.exe Tools/anim_export/make_md.lua              # 產生本文件
```
