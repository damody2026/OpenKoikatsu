# Unreal：KKS 左右手姿勢

## 來源與使用

KKS Studio 的 `OCIChar.ChangeHandAnime` 呼叫 `ChaControl.SetShapeHandValue`／`SetEnableShapeHand`。它使用 `chara/oo_hand.unity3d` 的 `cf_anmShapeHand` 和 `cf_customhand`，在 Animator 之後覆寫 30 個手指目標的 local rotation。本機實際資源有 24 組鍵，左右手各 15 個目標。

已匯入的 42 列 `HandAnime` 是舊 controller 資源目錄；本版 Studio 手勢不經該 controller 追加層。原目錄資料保留。

3D Gallery 左欄「左右手動作」可直達動作欄頂部；左右手各有一組「動作甲 → 乙」選單與混合滑桿，以及各自的「跟隨動作」重設按鈕：

- A 選「跟隨動作」停用該手覆寫，還原當前遊戲動畫的手指姿勢。
- A、B 選手勢索引；滑桿 0 取 A，1 取 B。
- 數量從目前遊戲／override bundle 讀取；沒有按角色名稱硬編索引或補偏移。
- 換卡／換 outfit 保留目前選擇，按新資源數量限制索引。

Blueprint 的 `KKPreviewActor.GetHandShapeCount()` 查數量，`SetHandShape(Side, Enabled, A, B, Blend)` 設定。Side 0 左、1 右。Blend 依 Unity `Mathf.LerpAngle` 夾在 0..1；非法 side／key／非有限 blend 回傳 false，保留原設定。索引以原序列位置判斷，不是鍵裡的 `no`。

手勢切換重新計算同一動畫影格，保留動畫時間與 DynamicBone 歷史。靜止模式透過 StaticInteractionFrame 重算 shaped baseline 與手勢，portable 角色不需要 game_root；播放模式由 ApplyPoseFrame 的原生／Unreal Animator 路徑計算。單次與批次更新採相同規則。停用不把上一個手勢當 rest；乙選單與混合滑桿在跟隨動作時停用。ShapeHandInfo 只寫 rotation；位置、縮放、對側手保留，再由既有 ExpressionBone／ABMX／物理流程處理。目標限定在 body rig，私有 mod 同名骨不能借此重綁。

## 遊戲正向控制

`Tools/GamePoseProbe/Probe.cs` 在 `OK_PROBE_HAND_SHAPES=1` 時，以实际 `ChaControl.UpdateAlwaysShapeHand` 輸出：

- 左右手全部 24 組鍵，混合 0.3／0.8、超界 -1／2，以及 disabled 控制，共 58 cases。
- 每例 30 指骨與 4 個不應改變的 hand／forearm joints。
- 用不同的初始 rotation，確保停用／對側不是偶然與預期相同。

原始 fixture：`Saved/Tests/HandShapes275/GameHandShapes.json.hand-shapes.tsv`；JSON 的 error 必須為空。

```powershell
cd C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test shape::hand:: -- --nocapture
cargo test all_hand_shapes_match_installed_game -- --ignored --nocapture
cargo test all_dump_cards_hand_shapes_cache_and_palette -- --ignored --nocapture
```

實際 game 控制共比對 1,972 筆骨骼資料；最大四元數分量誤差 0.000000119，門檻 2e-5；local position／scale 逐值相同。兩個純定向控制與 game fixture 通過，log 位於 `Saved/Logs/HandShapeTargeted275.log`、`HandShapesGameControl275.log`。

## Unreal 驗證與截圖參數

2026-10-08 新增 `Tools/run_editor_acceptance.ps1 -Tag 新標籤 -Card Content/CharacterCards/CrowdAPNG/C000.png -Mode Hands`。HandsR3 實際經由 UI callbacks 驗證靜止 portable 角色的全部左右手選項、混合、單側重設、demo 播放／切換與半脫重載，共八項通過。最終截圖確認左欄直達入口與右欄獨立選單、完整欄寬的重設按鈕；完整結果與正式 DLL 雜湊見 `Saved/Tests/EditorAcceptance/hands-final-r3.json`，問題紀錄見 `Docs/CHARACTER_EDITOR_ERRORS.md` E44–E46。

Automation：`OpenKoikatsu.AnimationCatalog.HandShapes`，實際 Alice session 播放 Studio walk 0.4 秒。覆寫左手、檢查對侧／其他 Animator joints、非法選擇不變、停用恢復當前 frame。`ImportedData`／`Playback` 同時覆蓋先前完整匯入與 mod 動畫。

原生 Gallery 驗證：設 `OK_AUTO_3D=1`、`OK_AUTO_CARD=Alice_keiyuu`、`OK_VERIFY_HAND_SHAPES=1`，執行 `ok.VerifyAnimationCatalog`。經真正 combo／slider callbacks 選左 1→23 / 0.3、右 23→1 / 0.8，核對 clock 不跳，再等待 shader 編譯並拍 UI。

五卡截圖用 `ok.ShotPreview`，`OK_SHOT_CARDS` 指五張原卡完整路徑，`OK_SHOT_OUTPUT_DIR` 指 `Saved/DebugShots/HandShapes275`：

```text
OK_GPU_SKIN=1
OK_SKELMESH=1
OK_SHOT_STUDIO_ROW=r00000022
OK_SHOT_ANIM_SCENE=4
OK_SHOT_ANIM_TIME=0.4
OK_SHOT_MOUTH_PATTERN=8
OK_SHOT_MOUTH_OPEN=0.8
OK_SHOT_HAND_LEFT=1
OK_SHOT_HAND_LEFT_B=23
OK_SHOT_HAND_LEFT_BLEND=0.3
OK_SHOT_HAND_RIGHT=23
OK_SHOT_HAND_RIGHT_B=1
OK_SHOT_HAND_RIGHT_BLEND=0.8
OK_SHOT_ATTACHMENT_AUDIT=1
```

負 A 值停用该手。每卡各全身／正臉／側臉。當幀 geometry 配合玩家實際投影取景，不改角色比例。

## 範圍

最終 `HandShapesFiveCards275Final.log` 五卡共 240 selections、GPU palette／停用還原／cache／clock 全通過（132.95 秒）；`HandShapesUnrealAutomation275Final.log` 三項 Success，`HandShapesUnrealBuild275Finalv2.log` 編譯成功。五卡 15 張與 Gallery 圖全部已目視。

`Tools/lua/lua.exe Tools/GamePoseProbe/verify_hand_shapes.lua` 核對原生 fixture、成功 logs、15 PNG 名稱／尺寸、71 display audits、15 projection fits 和全身前景 bbox，產生 `Saved/Logs/HandShapes275Audit.json`。最大 Unreal 對 extractor 顯示頂點誤差 0 cm；全身至少 51 px 下緣。初輪錯誤與修正保存於 `Docs/ERROR_LOG.md` E-131，來源與完整結果在 `HANDOFF2.md` §132。

這個來源控制驗證 KKS 手勢演算法；Unreal screenshot／attachment audit 驗證顯示接線，不能拿它們宣稱遊戲逐像素等價。完整 Animator graph／blend tree／additional layers、Eyes／Eyebrow controllers 和 `HANDOFF2.md` §130 的其他 graph／hook 缺口仍需各自實作與正向控制。
