# 原生 FBS 與 Unreal 嘴型控制

## 缺眉毛的探針問題（2026-09-28）

使用者指出的 `GameMouthMarchFrame.json.mouth-0-0.8.png` 是遊戲原生探針圖。
當時新建 Camera 只做 `CopyFrom(Camera.main)`，漏掉本機啟用的
`Eyebrow Outline Fix`（`KK_Fix_Eyebrow.cfg: Enabled=true`）的繪製流程。

反編譯來源位於遊戲 `kks_decomp/plugins/IllusionFixes/KKS_Fix_Eyebrows/IllusionFixes/`：

1. `Blitty.Start` 保存 `Camera.main`，並按 renderQueue 排列材質。
2. `OnWillRenderObject` 暫停眉毛／眼線 renderer 的一般繪製。
3. `OnRenderObject` 以 DrawRenderer CommandBuffer 畫入 `EyebrowFix.rt`，
   buffer 掛在保存的主相機 `AfterForwardOpaque`。
4. `Hooks.AmplifyColorBase_OnRenderImage` 將 rt 合成至主相機影像。

新建 Camera 沒有上述 CommandBuffer 和後處理元件，因此眉毛／眼線消失。
原生 weights、renderer enabled/active 與材料仍存在，不能誤判成卡片缺資源。

`Tools/GamePoseProbe/Probe.cs: CaptureMouthFace` 改用實際主相機 Render，
拍攝後還原相機設定、位置／旋轉與原 render target。
不關閉插件，也不按卡名補眉毛。每張圖旁保存 renderer components、
主相機 components／buffers 與 channel weights。
`RunMouthShapeControl` 等 WaitForEndOfFrame，並固定眼／眉 pattern0 的
OpenMax，避免同幀 GPU 尚未更新或隨機眨眼造成無效圖像對照。

正式原生圖：

- `Saved/Tests/MouthShapes276/GameMouthMarchSourceFinal.json.mouth-0-0.8.png`
- `Saved/Tests/MouthShapes276/GameMouthMarchSourceFinal.json.mouth-8-0.8.png`

早期缺眉或尚未更新的圖保存於同目錄 `old/`，不得當正式來源對照。

## FBSBase 權重語意

原生 `FBSBase.CalculateBlendShape` 將 open amount 乘 100、Clamp 到 0..100、
再轉 int；close=100-open。指定手動固定開口時，Unreal CPU 和 GPU 使用
`OKFbsOpenPercent` 的相同整數百分比。例如 0.61337 是 close39/open61。
每次清除本控制器所有 channel，再套目前 close/open；同一 channel 的兩權重相加。
缺失的 -1 index 不寫。這裡未宣稱完整時間交叉混合／眼眉控制器已接線。

原生 fixture 有 5 targets、各 54 patterns、10 cases、3120 channel weights、
33150 BakeMesh vertices。嘴型測試比較其中 2360 個嘴型 channel（包含清零），
不混入其他眼／眉控制器的 channel。

CPU procedural 嘴型將相同 deformed positions 同步至 surface／overlay／outline
所有 sections，保留各 pass 的 offsets、UV、color、normal、winding，
並在拓撲不一致時報錯。兩次變形和還原測試驗證不會累積 offset。

## 驗證與限制

- `OpenKoikatsu.FaceBlendShape.NativeWeights`：原生權重控制。
- `OpenKoikatsu.FaceBlendShape.SharedDeformation`：多 pass 的相同頂點流。
- 同輪保留 AnimationCatalog HandShapes／ImportedData／Playback 回歸。
- 五張 dump 卡共 15 張 Unreal 圖：`Saved/DebugShots/MouthFbs276/`。
- `Tools/GamePoseProbe/verify_mouth_shapes.py`：驗證完整資料、buffer／Blitty
  證據、逐項 Automation Success、圖片數／尺寸和 display attachment audits。

Stage276 的嘴型8下唇紅色／白色突片尚未修正；後續根因與通解見下節。
第一輪關輪廓控制受 static 初值快取污染，已修正並保留無效比較紀錄。

## 嘴部基準與 doubleTooth（Stage277）

`apply_default_fbs` 原先沿用離線 OBJ 預覽的預設 mouth0/open80。
Unreal CPU `ApplyMouth` 則假定 BaseVertices 已是 pattern0/closed100，
先扣除 closed delta，再加指定嘴型。兩個基準不同，導致每個嘴型都多出
固定偏移；下唇外的紅／白突片分別來自被錯誤變形的舌與牙。

Live session 現在讀取實際 head prefab 的 MouthCtrl/PtnSet：

- CPU 頭部先清除此控制器涉及的 channel，再烘焙 PtnSet[0].Close=100。
- GPU morph 頭部保留 raw 頂點，由 runtime 寫相同控制器權重。
- 所有 authored mouth channels 的 delta 都轉換到同一座標系，mod 自訂
  channel 名稱亦可用。離線預覽仍可明確指定嘴型。
- 中性 FBS 採原生整數百分比；EyesOpenMax 保存讀取的 float bits，
  避免先 round 到千分位再跨越原生 int 邊界。

沒有依卡名、顏色、臉比例或擬合 affine 修正幾何。

原生 `ChaFileFace.doubleTooth` 預設 false；`ChaReference` 將
ObjDoubleTooth 指向頭部 GameObject `cf_O_canine`，`ChaControl` 按旗標
`SetActiveIfDifferent`。載入器讀取此旗標，優先以 renderer GameObject
身分辨識犬齒，即使 mod mesh 改名亦保留來源身分。SessionPart／PartBlob／
C ABI 的 `render_visible` 傳遞狀態，Unreal 跳過 inactive target 繪製，
保留其 FBS 與幾何資料。Session pipeline122 對應 cache layout 變更，
decoded prefab cache89 不變。

### 原生幾何控制

`RunMouthShapeControl` 額外輸出 `.mouth-world.tsv`：按遊戲实际 channel
weights 加 authored delta，再逐點計算 bone.localToWorld * bindpose
加權。單 frame／100%／權重和均有 gate，避開 BakeMesh scale 重複套用。
同時保存骨 world／bind matrices。Native 與 extractor 各以真正載入的
`cf_J_FaceRoot` 作座標框架，沒有擬合矩陣。

`compare_mouth_world.py --verify` 比較三月七 5 targets × mouth0/8 ×
4 distinct rates，共40組。原生 closed/open 差值識別嘴型支持頂點：
每例共1447點；最後重跑最大誤差2.30e-6公尺，所有牙／犬齒／舌完整網格
最大誤差4.10e-7公尺。共有骨矩陣最大分量差2.35e-7，門檻未放寬。
原始33150筆 native world vertices、Rust fixture及報告保存在
`Saved/Tests/MouthWorld277/`、`Saved/Tests/MouthShapes276/RustMouthMarch.tsv`
和 `Saved/Logs/MouthWorld277Comparison.json`。

重現：

```powershell
cd C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib native_mouth_geometry_fixture -- --ignored --nocapture
cd C:/portable/OpenKoikatsu
Tools/lua/lua.exe Tools/GamePoseProbe/compare_mouth_world.lua --verify
Tools/lua/lua.exe Tools/GamePoseProbe/verify_mouth_basis.lua
```

其他四卡以實際 Unreal 五卡回歸驗證；不能把單卡 native 數值比較稱為
所有卡的遊戲像素等價。三月七非嘴型眼區仍有約2.43毫米的控制狀態差，
完整 Eyes／Eyebrow controller、Animator graph 與其他載入缺口仍待完成。

### 最終 Unreal 回歸

- `MouthBasisUnrealBuild277Final.log`：成功，30.47秒。
- `MouthBasisUnrealAutomation277Final.log`：HandShapes／ImportedData／
  Playback／NativeWeights／SharedDeformation 五項逐一 Success。
- `DoubleToothCard277Finalv2.log`、`DoubleToothCache277Final.log`：
  五卡旗標、true/false、partial update、mod renderer 身分／decoy、
  inactive FBS cache 還原均通過。
- `Saved/DebugShots/MouthBasis277/`：五卡各全身／正臉／側臉15圖，
  已全部目視；嘴外突片消失，眉／眼線保留，無新增附件脫離。
- `MouthBasisShots277Finalv2.log`：71次display attachment audits，
  最大0cm；15個投影fit，全身下緣至少43px；source inactive canine
  六次skip，含preflight及五卡。
- `MouthBasis277Audit.json`：完整數值與圖片資料核對，來源hash亦保存。

舌頭材質仍比原生偏紅，部分下巴輪廓斷續；這是後續材質／渲染缺口。
目前 tang 材質載入仍有强制solid色及早退，尚須來源控制隔離，不能把
幾何基準已修正誤稱全部 shader 已對齊。所有失敗與撤回推論見 ERROR_LOG E-133。

## 來源 draw 材質／mod shader 分支（stage278）

上述固定舌色早退已移除，依實際shader及material載入MainTex、ColorMask、
三／四個Color與獨立ST，Character的ME coordinate固定0。原始MainTex不再
預先烘固定粉紅；ME normal轉換亦沿用來源runtime blit。

本機MaterialEditor實際DXBC：

- KKS Koikano/main_clothes_item PS0024有Color4；
- EC Shader Forge/main_item PS0005只有Color1/2/3。

兩者依mask.r/g/b連續lerp再乘MainTex，Koikano另依1-max(rgb)混Color4。
Session/cache/C ABI保存分支；pipeline125，decoded cache89。改名mod mesh、
coordinate-zero、MEtexture/Color4及cache roundtrip通過。三項Rust測試、
五項UE automation均通過；forced TestExit的完整stdout為automation證據。

五份fresh actual-game controls指定原始card path、單卡載入前／8秒後核對：

| 卡 | 原生shader | 混色數 |
|---|---|---|
| March、KK_091207 | Koikano/main_clothes_item | 4 |
| Alice、Yuuka、KK_384283 | Shader Forge/main_item | 3 |

各卡Color/ShadowColor與UE輸入一致，ColorMask全紅、null MainTex使用white。
不要猜Forge→Koikano alias。原生資料在Saved/Tests/MouthMaterial278。
舊dump的Alice外層包含Hayase Yuuka V1.02_0，Yuuka外層包含Alice_0-tpose，
材質色值亦交叉；核對資料須保存內層角色與完整hierarchy，不能只按外層名字。

最終Saved/DebugShots/MouthMaterial278五卡15圖已目視；眉／眼線保留、
舌頭改用來源色，側髮／髮飾／頸飾跟隨。71 attachment audits=0cm、
15 projection fits≤0.92、全身下緣至少43px。40組native嘴部比較仍為
2.295458e-6m，門檻不變。verify_mouth_material.py與MouthMaterial278Audit.json
保存source材質、舊dump不匹配項及完整gates；詳細見HANDOFF2§135、E-134。
完整光影、chin outline及完整眼眉控制器仍是獨立剩餘工作。

## 控制器眼開度上限與整臉幾何（stage279）

March卡片Status.eyesOpenMax=1，遊戲卻使用8/92眼權重。根因是
ChaControl.ChangeEyesOpenMax分開保存status（上限1）與FBS控制器（上限.92）；
舊loader漏掉第二個clamp。現對所有卡按同一原生規則建立controller baseline，
保留卡值及精確float bits，再按FBSBase整數percent量化。pipeline126使舊cache失效。

```powershell
cd C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib neutral_fbs_integer_percent_matches_native -- --nocapture
cargo test --release --lib native_mouth_geometry_fixture -- --ignored --nocapture
cd C:/portable/OpenKoikatsu
Tools/lua/lua.exe Tools/GamePoseProbe/compare_mouth_world.lua --verify-all
Tools/lua/lua.exe Tools/GamePoseProbe/verify_eye_clamp.lua
```

40組比較現要求全部臉／眼／牙／舌頂點，小於原2e-5m gate。原眼區2.43mm
差異消失，整體最大2.295458e-6m。這是March的固定neutral eyes/brows控制；
完整Eyes/Eyebrow pattern、blink/gaze與時間混合仍有後續工作。
Unreal五卡回歸與資料核對保存EyeClamp279Audit.json，參見HANDOFF2§136/E-135。

最終EyeClamp279五卡15圖全部目視，眉／眼線及附件跟隨維持；71 display
attachment audits最大0cm、15投影fit≤.92、全身下緣至少43px。五項UE
automation逐一Success，build與capture正常完成；完整stdout作automation證據。
重現參數與限制見Saved/DebugShots/EyeClamp279/README.md。

## Mod 眼眉中性基準的來源索引（stage280）

眼／眉基準亦從選中head prefab的EyesCtrl／EyebrowCtrl讀PtnSet，
不以_def_cl/op或mayu名稱判通道。先reset引用channels，再按pattern0
close/open累加，shared-index為100，-1不寫；控制器相交按眉→眼覆寫。
眉OpenMax讀卡Status並clamp[0,1]，眼仍[0,.92]。pipeline127／decoded89。

五卡selected head的三個FBS控制器共2456筆patterns，Rust與UnityPy
逐筆一致、保存來源SHA256；異名／重排／shared-index／sentinel控制通過。
March40組mouth targets全部頂點（含臉眼區）max2.295458e-6m，
不是独立眉／iris或所有卡表情數值認證。五項UE tests通過、五卡15圖
全部目視，71display audits=0cm、15fit≤.92；verify_head_fbs.py完整通過。
圖與重現參數見Saved/DebugShots/HeadFbs280/README.md，錯誤E-136。
完整眼眉pattern切換、blink/gaze及blend-time仍需實作。

## 實際皮膚outline shader分支（stage281）

本機compiled Koikano/main_skin的outline vertex stage使用
`(.1*distance_m+.6)*.003*_linewidthG*_LineWidthS*(1-DetailMask.b)`，
不是原先共用hair的`.3/.005`。Shader Forge/main_skin仍為後者且沒有
`_LineWidthS`。KKS／EC的DXBC及binding已核對；Source kind5/7分開處理，
材質scalar保留ME覆寫、Session/cache/C ABI，pipeline128／decoded89。
native March的face `_LineWidthS`載入前／後均1，JSON error空。

DetailMask外擴抑制改GPU vertex LOD0、numeric linear／bilinear及各自ST；
移除CPU nearest、忽略ST與8bit vertex-alpha量化。沒有按卡片改比例或
chin offset；source triangles及normals維持。完整非均勻object scale與
fragment光影還有獨立缺口，不宣稱全部outline頂點／pixels已等價。

Scalar0/.37/1/1.8與改名mesh的cache控制通過，新geometryfixture35.59秒
通過、40組Marchmouth targets全頂點max2.295458e-6m；UE最終build10.69秒
成功，五項automation Success。初版缺Subtract header的失敗保留在E-137。
`Tools/lua/lua.exe Tools/GamePoseProbe/verify_skin_outline.lua`核對來源與最終五卡回歸。

最終SkinOutline281五卡15圖全部目視，眉／眼線、側髮／髮飾／頸飾保留；
71display audits=0cm、15fit≤.92、全身下緣至少43px。完整verifier通過，
SkinOutline281Audit.json保存證據；圖與參數見該資料夾README。部分chin細線
仍斷續，完整渲染差異尚未閉環，不能只因公式／tests通過就宣稱全部解決。

## 眼型／眉型切換與手動開度（stage282）

Gallery現在有「眼型／眼睛開合」與「眉型／眉毛開合」。資料直接來自
各卡實際head的EyesCtrl／EyebrowCtrl PtnSets，不用vanilla通道名稱猜mod。
五張驗收卡Eyes分別30、28、28、30、28組，Brow均17組。嘴型與來源動作
可同時使用。Blueprint `SetFaceControl(Control, Pattern, Open01)` 的Control
為1=Eyes、2=Eyebrow；非法pattern／controller／非有限rate會拒絕。

此滑桿控制來源FBS的FixedRate，不是普通eyesCtrl.OpenMax。FBS先清全部
引用channels，再以整數open百分比寫Close/Open；同index相加100，-1略過。
眉→眼→嘴覆寫次序與遊戲一致。CPU display從baseline加差值，secondary
sections也更新，還原不累積形變。Session/cache/C ABI保存真正索引、每卡
預設rate及已烘焙baseline，pipeline132／decoded89。

FBS引用的inactive mesh仍保留metadata：第一次native測試發現旧loader
丟掉tears。現在actual引用優先於detail名稱白名單，draw visibility另管；
遊戲tearsLv=0仍隱藏三種tears，選眼型不會改其等級。tear等級UI尚未加入。
Visibility依renderer的GameObject reference，mod改Mesh asset名不會破壞判斷。
眼線第二材質另發出的_kage pass保存實際source key，繼承控制器、baseline
與所有轉換delta，閉眼時shadow一起移動；不是對display名字猜資料來源。

原生March141組／mod Alice135組所有眼眉patterns與rate0/.61337/1，
共145524 channel權重／1096 pairs與UE helper一致。另核對兩個head的58組
第二材質pairs與其全部delta／baseline。UnityPy重新獨立核對
五卡2456 source patterns。六項UE tests通過，March40組mouth-target全頂點
原生geometrymax2.295458e-6m。後者不是所有獨立眉／iris的worldgeometry。

`Tools/lua/lua.exe Tools/GamePoseProbe/verify_face_controls.lua`完成來源、native、cache、
build、引擎、UI與capture驗收。五卡15圖和Gallery截圖已目視，變形還原0cm、
71附件audit0cm，Eye/Brow控制真正callback可切換，時間未因setter前進。
圖在Saved/DebugShots/FaceControls282與FaceControls282UI；錯誤E-138。

使用者暫緩下巴輪廓線，本輪未改。ChangePtn時間混合、automatic blink/gaze、
完整Animator graph與shader像素等價仍需另外驗證。GPU component有相同
weight-difference更新路徑，本輪五卡capture是預設CPU-head，不能當GPU全
蒙皮／outline認證。來源數值、nativeweights與UEdisplay各有明確證明範圍。

## stage283：來源時間混合與自動眨眼

FBSBase.ChangePtn使用TimeProgressCtrl的.15秒線性混合。相同目標不重啟，
中斷取上一個「目標」作Back；先量化當前開度，再乘兩個pattern權重，
維持眉→眼→嘴的寫入順序。Tick在pose更新後推進，face時間與clip分開。

FBSBlinkControl保留三狀態、source隨機區間、逐幀close count、fixedFlags
與frequency0。選中head的BlinkFrequency/BaseSpeed、Brow SyncBlink及
Eyes/Brow OpenMin／FixedRate與card eyesBlink經Session/cache/C ABI傳入，
pipeline133／decoded89。普通眼OpenMax=.92與手動FixedRate仍分開。
UI提供自動眨眼與眨眼一次；auto時眼眉rate滑桿禁用，pattern仍能混合。

Blueprint：`SetMouthPattern(Pattern,bBlend=true)`、
`SetFacePattern(Control,Pattern,bBlend=true)`、`SetAutomaticBlink(Enabled)`、
`ForceBlink()`。固定API `SetFaceControl(Control,Pattern,Open01,bBlend=false)`
保留立即開度用途；auto時連續呼叫此API會用手動rate優先於blink。
換卡依actual head驗證pattern，沒有角色名称分支。

Fresh March／mod Alice：144時序frames／75888通道權重、16settings／
1096pairs、480blinkframes與UE helper一致。blink測試注入原生實際亂數，
不要求UE重現Unity RNG序列。最終七項UEtests通過；五卡15張半閉眼、
兩卡12張睜閉端點、Gallery真callback全部目視，眉毛保留，變形還原0cm，
71附件audit0cm。來源／cache／build／native／geometry／display的驗收與
hash存`Saved/Logs/FaceTime283Audit.json`，錯誤E-139，HANDOFF2§140。

重現（專案根目錄）：

```powershell
Tools/GamePoseProbe/build.ps1
Tools/GamePoseProbe/run_face_time_native.ps1
Tools/GamePoseProbe/run_face_time.ps1 -Mode Tests
Tools/GamePoseProbe/run_face_time.ps1 -Mode Shots
Tools/GamePoseProbe/run_face_time.ps1 -Mode Shots -Blink closed -Cards Primary
Tools/GamePoseProbe/run_face_time.ps1 -Mode Shots -Blink open -Cards Primary
Tools/GamePoseProbe/run_face_time.ps1 -Mode Gallery
Tools/lua/lua.exe Tools/GamePoseProbe/verify_face_time.lua
```

Source：遊戲`kks_decomp/Assembly-CSharp/FBSBase.cs`、
`FBSAssist/TimeProgressCtrl.cs`、`FBSBlinkControl.cs`與`FaceBlendShape.cs`。
SetSpeed的來源Max(1,value)按原生結果保留。gaze修正、tear等級UI、嘴寬
隨機、完整Animator graphs、GPU-head完整顯示與shader像素等價未由本項
認證；下巴依使用者要求延期。

## Stage284：視線修正與動態瞳孔 UV

`SetEyeGaze(LeftHorizontal, RightHorizontal, Vertical)`輸入遊戲EyeLookCalc的
normalized rates（[-1,1]），可由Blueprint或Gallery三個視線滑桿控制。
這是來源角度率的下游控制；world target求角與眼球骨骼旋轉尚待實作。

FaceBlendShape按左眼H與共用V做sqrt(t)開度修正，讀actual selected prefab的
Up/Down/Side；五卡都是0/.33/.17，中央最大開度.92，測試方向
(.6,-.4,-.4)為.6164。Blink在OpenMin到correctMax間插值，FixedRate優先。
Session保存12個來源runtime值，pipeline134／decoded89。

EyeLookMaterialControll的14個來源參數依actual renderer取得；序列化空指標
按ReSetupMaterial從同GameObject取Renderer，左右眼用eyeLR。Card pupil值
設定offset／scale，Mathf.Lerp的gain採Clamp01。原始UV保留，依
MainTex ST重算UV0，Clamp採樣交給sampler；主／secondary sections同步，
不改UV1/2或對上一次視線結果累積變換。

原生March與mod Alice共400case（blink0/.5/1、固定開度、眼型0/1/8），
UE核210800channel weights、143200UV vertices、80settings／1096pairs；
額外400native材質公式控制由Rust比對。8項UEtests全通過，五卡15圖＋
Gallery UI全部目視，視線roundtrip position／UV誤差0，71附件audit0cm。
原40組mouth-target世界頂點誤差max2.295458e-6m，未放寬2e-5m門檻。
完整驗收與hash在`Saved/Logs/EyeGaze284Audit.json`；錯誤E-140、HANDOFF2§141。

重現（專案根目錄）：

```powershell
Tools/GamePoseProbe/build.ps1
Tools/GamePoseProbe/run_eye_gaze_native.ps1
Tools/GamePoseProbe/run_eye_gaze.ps1 -Mode Tests
Tools/GamePoseProbe/run_eye_gaze.ps1 -Mode Shots -Blink open
Tools/GamePoseProbe/run_eye_gaze.ps1 -Mode Gallery
Push-Location Tools/ok-extract
cargo test --lib native_eye_material_transforms
cargo test --lib face_controller_metadata_survives_cache
cargo test --lib eye_look_iris_uv_rest_pose
cargo test --release --lib native_mouth_geometry_fixture -- --ignored --nocapture
Pop-Location
Tools/lua/lua.exe Tools/GamePoseProbe/verify_eye_gaze.lua --contact
Tools/lua/lua.exe Tools/GamePoseProbe/verify_eye_gaze.lua
```

Source：遊戲`FaceBlendShape.cs`／`MathfEx.LerpAccel`／
`EyeLookMaterialControll.cs`／`ChaControl` pupil setters與head controller assignment。
GPU-head完整顯示、world-target／eye bones、highlights shader與全pixel等價、
tear等級UI、嘴寬隨機、Animator graph仍需各自處理，下巴仍延期。

## Stage285：來源 EyeLookCalc 與每幀 head ABMX

`eye_look.rs` 移植 `EyeLookCalc.EyeUpdateCalc`，採載入的controller PPtr、
root／center／eyeTransform引用、eyeLR與全部狀態參數。head rig先依
Sideloader `studio/base/00.unity3d` override，再使用vanilla／body fallback；
rootNode空沿用來源component transform，center空不執行雙眼correct。
不按卡片名稱、頭型、髮色或mesh名稱選擇演算法。

| 本次rig的pattern index | source lookType | 行為 |
|---|---|---|
| 0 | FORWARD | root前方，來源5度校正 |
| 1 | TARGET | 追蹤目標 |
| 2 | AWAY | 保留來源sorasiRate與兩眼共享比例 |
| 3 | NO_LOOK | 固定最後rotation，保留角度／重新算rates |
| 4 | TARGET | 第二組追蹤狀態 |
| 5 | CONTROL | targetObj存在時使用MoveTowards |

表格描述實際來源資料；production讀序列化states，不硬寫這六個index的參數。
保留左右不對稱limits、threshold／bendingMultiplier／maxAngleDifference、
nearDis、角度限制fallback、dt*leapSpeed的Mathf.Lerp、OrthoNormalize／Slerp。
center的InverseTransformPoint／TransformPoint與children localRotation按完整
非等比matrix計算，沒有以平均scale取代。向量Angle零向量回0、Sign(0)=+1。

### 初始化與真正的骨架綁定

`EyeTargetL/R` 是來源rig的輔助計算節點，與 `cf_J_Eye_rz_L/R` 同層，
不是實際iris／face mesh的祖先。五卡原生所有renderer的非零skin weights
都未綁到EyeTargets或其descendants。它們的rotation用於算normalized H/V，
再交給stage284的pupil UV／eye correctOpenMax；不可直接旋轉真正臉／眼球。
這更正stage284末段將「eye bones rotation」當成缺少幾何變形的假設。

Awake的referenceLook/up與origRotation，從實例化rig、CopySameNameTransform／
shape／ABMX之前保存。`Init`與Config經session cache保存，runtime角度／dirUp
每個session獨立，cache載入後重新初始化。SESSION_PIPELINE_VER=137、decoded89。
`head_shaped`在apply_shape中是ABMX前的baseline。以前apply_anim_frame還原它後
只重套body ABMX，使三月七EyeTarget相關parent Y少2mm；現在每幀也從同baseline
重套head ABMX，然後refresh skeleton／accessory／gaze。没有人工2mm或角色例外。

### Unreal API與UI

- C ABI尾端：`eye_look_pattern_count`、`step_eye_look`。target是preview actor
  的Unity座標空間（公尺）；API回左H／右H／V，沿用raw UV重新計算，非累加。
- `AKKPreviewActor::SetEyeLookTarget(WorldTarget,Pattern,Enabled)`轉換Unreal
  世界target，`AdvanceEyeLook(dt)`在body pose後／FBS更新前執行。
- `FollowEyeLookCamera`逐frame取StudioCam位置；Gallery的 **Eyes follow camera**
  checkbox有實際callback。`StopEyeLookTracking`保留最後rates；手動滑桿停止追蹤。
- 追蹤、手動視線、眨眼與嘴型均不推進clip clock；眼皮與secondary sections
  沿用來源FBS覆寫順序／desired-minus-baked-baseline。

### 驗證與重現

五卡原生trace在 `Saved/Tests/EyeLook285`，每卡144frames，共720；包括六種
states、近距離／背面／上下目標、disabled、control object，以及head旋轉／
非等比scale。最大的normalized rate誤差3.71933e-5、角度.0010976046度、
up6.737653e-6、quaternion component9.590294e-6、target2.3841858e-7m。
各項用分別gate，沒有把角度的單位當UV或position tolerance。

`native_eye_look_session_basis`另實際載入五卡，核Awake references／origRotation、
動畫後center-local眼位置（2e-5m）與root-relative parent rotation。
它曾拒絕漏head ABMX的pipeline135版本；修正後全五卡通過。
最終build／9項UE tests／15圖與UI驗收使用137，舊135診斷移入old。
原生mouth-target全40組vertices的2e-5m gate繼續使用，最大2.295458102745069e-6m。

```powershell
& Tools/GamePoseProbe/build.ps1
& Tools/GamePoseProbe/run_eye_look_native.ps1
Push-Location Tools/ok-extract
cargo test --lib eye_look::tests::native_eye_look_frames -- --nocapture
cargo test --release --lib eye_look::tests::native_eye_look_session_basis -- --ignored --nocapture
Pop-Location
& Tools/GamePoseProbe/run_eye_look.ps1 -Mode Tests
& Tools/GamePoseProbe/run_eye_look.ps1 -Mode Shots -Blink open
& Tools/GamePoseProbe/run_eye_look.ps1 -Mode Gallery
Tools/lua/lua.exe Tools/GamePoseProbe/verify_eye_look.lua
```

Source：`EyeLookCalc.cs`、`EyeLookController.cs`、`EyeObject.cs`、`EyeTypeState.cs`、
`ChaControl` load order、CommonLib.CopySameNameTransform、ABMX BoneController。
默认CPU-head五卡顯示是本輪範圍；GPU-head、iris／highlights shader pixels、tear UI、
嘴寬隨機與完整Animator graph仍需各自驗證，下巴繼續延期。

## Stage286：眼睛 draw 高光與完整 shader 身分

原生五卡材料表確認兩路：`Koikano/main_eye`（March、KK_091207）及
`Shader Forge/toon_eye_lod0`（Alice、Yuuka、KK_384283）。這張表是驗收資料；
生產載入按 shader 名稱分流，不讀角色名。Unity `Shader` 的 `m_Name` 可只是資產標籤，
完整 `Shader.name` 來自 `m_ParsedForm.m_Name`，reader優先使用後者。Decoded cache90／pipeline140。

`EyeDraw` 保留高光貼圖、卡／ME顏色、X/Y位移、rotation、isHighLight與原始UV。
`EyeLookMaterialControll.Update` 對上下高光使用和瞳孔相同的scale／gaze偏移，
再加各自的卡片HL offset；右眼只鏡射pupil offset，HL X不鏡射。
原始UV缺失時用Unity的零值，不把主UV複製到另一個channel。

| 項目 | Koikano | Shader Forge |
| --- | --- | --- |
| Main sample | texcoord ST → Main ST → rotation | rotation → Main ST |
| HL sample | UV1/2 texcoord ST → HL ST → main rotation + HL rotation-.5 | UV1/2 → HL ST |
| HL RGB | max(texture.r × texture.a × color.rgb) | max(texture.a × color.rgb) |
| HL alpha | max(texture.a × color.a) × isHighLight | 同左 |
| Main color | 乘材質 `_Color` | shader不讀 `_Color` |

以上UV運算均為affine，對vertex streams作用後再插值與來源片元次序等價；
sampler clamp保留在GPU。原始controller ST診斷UV與實際shader sample UV分開保存。
viewer的每次SetEyeGaze更新三組sample UV，嘴型／表情更新與骨架動畫不得覆蓋這些channel。
來源Color property在Unreal常數輸入轉linear，alpha不做gamma轉換。

來源：`ChaControl.ChangeSettingEyeHL*`／`ChangeSettingEyeTilt`、
`EyeLookMaterialControll.Update`，及已安裝KKS／EC shader bundle的
Koikano main_eye PS0008、Forge toon_eye_lod0 PS0001。ASM副本放Saved/Tests/EyeShader286。
原生probe輸出各卡25方向×2眼×3ST，以及三組raw UV、draw參數、20張貼圖readback。

```powershell
& Tools/GamePoseProbe/build.ps1
& Tools/GamePoseProbe/run_eye_shader_native.ps1
Push-Location Tools/ok-extract
cargo test --release --lib native_eye_draw_loaded_cards -- --ignored --nocapture
cargo test --release --lib eye_draw_material_cache_preserves_three_streams
Pop-Location
& Tools/GamePoseProbe/run_eye_shader.ps1 -Mode Tests
& Tools/GamePoseProbe/run_eye_shader.ps1 -Mode Shots -Pose Walk -Blink open
& Tools/GamePoseProbe/run_eye_shader.ps1 -Mode Shots -Pose Tpose -Blink open
Tools/lua/lua.exe Tools/GamePoseProbe/verify_eye_shader.lua
```

本輪範圍是兩路高光draw、rotation／ST和shader身分。完整expression、
Koikano overtex3、create_eye公式、光照尾段與全raster pixel等價仍須獨立驗收，
不因UV／材料數值與截圖通過便宣稱完成。未做下巴專用修正。

Stage286最終驗收：五卡750ST transforms、5370 raw UV vertices、20原生貼圖
尺寸／方向全部相符；RGBA最大平均誤差0.442928314/255。Release材質測試
143.13秒1passed、cache三streams測試1passed。UE9項全Success／exit0，
Walk／Tpose各15圖已目視，各71attachment audits0cm、15fits≤.92；
高光UV1／2各358vertices更新、三UV全部還原0。
完整證據Saved/Logs/EyeShader286Audit.json／EyeShader286Verified.log；
對照總覽Saved/Tests/EyeShader286/ContactSheet_{Walk,Tpose}.png。

高光資源透過與其他臉部細節相同的KKEx GUID／original slot解析，無resolve
則vanilla優先，限定指定list row的bundle。Merged catalog的同local ID可指向
不同mod，不能借head/body pool同名texture或resize錯圖來通過來源尺寸gate。
