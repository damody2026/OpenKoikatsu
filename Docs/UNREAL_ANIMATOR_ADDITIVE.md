# Unreal Animator Additive 層

## 來源與範圍

Stage297依KKS serialized AnimatorController、AnimationClip與原生Animator實作。`Tools/anim_export/inspect_additive_layers.lua`獨立掃437個base controllers：1946 layers，其中526個Additive（blending mode1），1420個Override。526個Additive均單一clip、非synced motion set0；base沒有Timing=true，只有一個非零default extra layer（本輪未自動載入它）。不按卡名或角色比例補丁。

遊戲`HandCtrl.cs:366/370`以`1-_ans`/`_ans`連續設定src/dst body layer weights；`2132`與`2157`啟用動作／idle layer。本轮提供base state加一個selected layer與0..1權重；完整HandCtrl多層scheduler與多active layers仍待後續。

## 共用載入

- `controller.rs`保留來源layer blending mode、SkeletonMask、state speed/cycle/loop，單一clip亦走composition。
- `controller@HEX`（UTF8 JSON `[baseSource,baseOwner,overlaySource,overlayOwner]`）生成任意來源base state配Additive layer state；label為`owner / [Controller layer N] BasePath + AdditivePath`。同一層多個Additive states各自生成；原同步Override同名配對規則保留。
- `studio@HEX`繼續遵循Studio source profile；`controller@HEX`保留base來源場景。本輪H Auto是H/esthetic，不插入Studio PV／ExpressionBone。
- `OK.LayerWeight`是runtime控制，與來源float namespace分開，預設1，0..1；原生controller同名或reserved id碰撞明確拒絕。既有FFI animation float列表／setter、Unreal `FOkSession::SetAnimationFloat`與Preview Actor可使用，無新ABI。無layer時不公開這個控制。
- Session拒絕負值、>1、NaN、Infinity，拒絕後參數與時間保持原值；改權重不改state duration。Override也依來源mask乘selected layer weight。

## 參考與TRS合成

對每個Additive leaf先取得來源reference，將pose轉成delta，再blend tree；來源mask乘layerWeight後對base合成。

- 無`m_ValueArrayReferencePose`時，取同clip的首sample；本輪3個原生來源及掃描526個Additive層皆空reference array。
- 有reference時，依generic binding dimensions的完整curve順序讀取；要求長度完全一致且finite。ClipEntry metadata與runtime override replacement後先檢查，格式錯誤不進public graph／listing／setter，不拿缺值0或卡片baseline補猜。此nonempty路徑有來源格式檢查，本輪native fixtures沒有覆蓋它。
- Position：`base + (sample-reference)*w`。
- Scale：`base * (1+(sample/reference-1)*w)`；未綁定TRS產生identity delta。
- Rotation：`delta=inverse(reference)*sample`，`weighted=normalize(delta.xyz*w,delta.w)`，`result=base*weighted`。分數權重不是Slerp或nlerp(identity,delta)；native layer8 .37/1.7顯示Slerp錯約1.82494e-4。
- SkeletonMask沿實際Animator hierarchy path hash傳遞權重，未包含的head／私有附件保留base，走既有Shape／gaze／髮與附件／乳搖／GPU pipeline。
- synced Timing=false既有base clock保留；Additive non-synced層用自己的state clock與base同elapsed，各自loop／clamp。

## 驗證與重現

Native控制來源：`h/anim/female/05_01_00.unity3d / khr_f_02_00`，base `Base Layer.OLoop`；Additive7/8/70（含loop與非loop），weights0/.37/1，elapsed0/.17/.61/1.7。每卡36frames，五卡180。每frame保存native合成、base、raw sample與首sample reference bones，並核對state沒有transition。

```powershell
# workdir C:/portable/OpenKoikatsu
Tools\lua\lua.exe Tools/anim_export/inspect_additive_layers.lua
Tools\lua\lua.exe Tools/anim_export/verify_native_additive.lua
& Tools/GamePoseProbe/build.ps1
& Tools/GamePoseProbe/run_additive_native.ps1 -Cards All
# workdir C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib additive_ -- --ignored --nocapture
cargo test --release --lib additive_weight_is_not_slerp_or_linear_quaternion
# build Unreal後；workdir C:/portable/OpenKoikatsu
& Tools/GamePoseProbe/run_additive.ps1 -Mode Tests -AdditiveOnly
& Tools/GamePoseProbe/run_additive.ps1 -Mode Tests -RegressionOnly
& Tools/GamePoseProbe/run_additive.ps1 -Mode Shots -Blink open -Cards All
Tools/lua/lua.exe Tools/GamePoseProbe/verify_additive.lua
```

正式RHI `Saved/DebugShots/AnimatorAdditive297_Layer8_open`：source H base OLoop+Additive8，weight.37、elapsed.4、mouth8/.8、blink open、gaze(.6,-.4,-.4)、Auto scene。這是指定來源composition的顯示驗證，非完整H scene、Studio OCI或全Animator scheduler。T-pose286根目錄15圖保持不變，下巴依使用者要求延期。

## 尚未涵蓋

多active weighted／Additive layers、HandCtrl item動畫、transitions／CrossFade、Directional2D／mirror／footIK、零／負播放speed、Timing=true、FK／IK、完整Studio options／scene、所有mod／shader pixel等價仍未全部完成。526層的source metadata audit不等同全部526原生逐格驗收；本輪正向控制3層×五卡。

## 正式結果

補齊reference guard後，五卡Rust兩項78.21秒PASS：native180frames／48600 TRS attrs，max position/rotation/scale皆1.1920929e-7；Session180frames／108000完整骨TRS／3905676 private attachment vertices，max骨5.9604645e-8、附件0m，2e-5門檻不變。

UnityPy獨立取來源mask並由原生base/raw/reference snapshots重新合成83988個Transform snapshots：position5.838631e-10、rotation9.671746e-8、scale0；錯誤Slerp1.824955e-4、nlerp1.997559e-4、absolute Override position.006372m皆被負向控制排除。`NativeCompositionAudit.json`保存數值，非與Rust共用實作。

rotation及異常reference metadata兩項unit PASS。第一版production staticlib／UE plugin build25.81秒Succeeded（已存BeforeReferenceGuard），最終guard版build34.01秒Succeeded，新UE再次Success。UE新test180frames、五卡均實際published palette1骨隨weight0→1改變（總5）、拒絕360個負值／>1 setter，全Success／QueueEmpty1／exit0。這5個palette骨改變與raw90骨×3的原生TRS證據用途不同，不把count當作全部mesh已逐像素一致。

最終guard版Unreal六項distinct tests均Success：新Additive180frames，加原1D450／2D300／clock180／StudioInitialization135／motion-time-scale300，共1545 source frames；QueueEmpty分別1/5、process exit0。RHI五卡15PNG900×1080、71attachment audits max0cm、15fits≤.92、424 follower samples、bone→palette max2.252e-6cm；五卡face time／gaze／highlight／EyeLook及source Auto scene gates全PASS。

PV display audit只核對四組source binding到palette/followers；metadata flags1111不是H source實際執行Studio PV。已看完整15圖ContactSheet及March全身、Alice／KK384正臉原圖；體型比例、眉眼、側髮、髮飾與頸飾未見新增偏移。目視是regression review，非遊戲同frame shader pixel認證。

`verify_additive.py`完整PASS，`Saved/Logs/AnimatorAdditive297Audit.json`／`AnimatorAdditive297Verified.log`保存來源、native fixtures、source/native／Session／unit／build／UE／capture logs、code／binary／PNG SHA256。root stage286 T-pose15圖hash不變。native probe已移回Tools，owned native與UE均退出，未存遊戲卡／場景／配置；新capture資料夾只有本輪15PNG＋README。E-153完整保留所有失敗／更正，包括錯誤TOS推定後找到真正的attribute計數根因。

## Stage298 後續

通用多active Additive／Override層載入與各層權重已加入，詳見 `UNREAL_ANIMATOR_MULTILAYER.md`。本文件的Stage297範圍與數值保留作歷史驗證；完整HandCtrl scheduler／item動畫／transitions仍未完成。
