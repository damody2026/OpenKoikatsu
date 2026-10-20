# Unreal 多個 active Animator layers

## Stage298 共用實作

對照 KKS `HandCtrl.cs` 的 `LayerBlend.Set/Proc`：進行手部動作切換時同時設 src=`1-ans`、dst=`ans`，body 與 item Animator 分別處理。此前選取一個額外層的載入描述無法表達這個 body pose。本輪提供通用多 active body layers 與可調權重；不含完整 HandCtrl scheduler／item animation／CrossFade。

`states:layers@HEX|baseLabel`：HEX 為 UTF8 JSON `[controllerSource,baseLabel,[selectedLayerLabel,...]]`。controllerSource 是既有 `controller@HEX` 或 `studio@HEX`，保留 base controller、runtime clip override 與 scene profile。selectedLayerLabel 使用原公開 graph 的完整標籤，必須指向同一選定 base；既有同步Override同名配對等來源可載入限制仍保留，不能用外層描述繞過 unsupported graph；最多32個額外層，禁止重复 layer index、unknown state／base、空集合、nested layers 描述。描述排列不決定合成順序。

- 每層使用來源 index、blending mode、SkeletonMask、motion graph、state speed／cycle／loop。依 layer index 升序合成。
- Base 只取樣一次；各 leaf 固定從同一 shaped baseline 取樣，再合成到前一層結果。不能每層重新載入 base 覆蓋累積姿勢，也不能拿上層結果當 Additive reference。
- `OK.LayerWeight.N` 對每個選定層提供0..1權重；未設為1。與來源 float names／IDs碰撞拒絕，沿用 animation float FFI／Unreal setter，無新ABI。
- Additive 使用Stage297首sample／explicit reference delta、rotation imaginary weight規則；Override對累積pose依來源mask×layerWeight做TRS interpolation。
- Non-synced各層保留自己的 duration／speed／cycle／loop，從同elapsed起播；synced Timing=false保留base normalized clock。不提供各層不同起播時間、transition或Timing=true。
- 整組顯示duration／loop取base。Studio options僅由明確studio@ profile啟用，controller@即使來源目錄是Studio也不自動改成OCI初始化。
- 無卡名特例；保留既有Shape、私有髮與附件、FBS／gaze／physics／GPU pipeline。下巴輪廓依使用者要求延期。

## 原生正向與負向控制

來源 `h/anim/female/05_01_00.unity3d / khr_f_02_00`、base `Base Layer.OLoop`。

| 組合 | 權重 |
|---|---|
| Additive7 +8 | .63/.37 |
| Additive7 +8 | 1/1 |
| Additive8 +70 | .37/.63 |
| Additive7 +8 +synced Override73 | .63/.37/.41 |
| Additive7 +8 +synced Override73 +74 | .63/.37/.41/.59 |

每組elapsed .17/.61/1.7，每卡15frames，五卡75。保存native multi-layer、native base、各層獨立weight1姿勢與原生clock／clip weights。UnityPy獨立解析來源mode／path mask；本輪5個layer的來源mask是binary，isolated layer pose用於重建已masked的貢獻。此獨立控制不代表所有mod的fractional mask已原生驗收。

## 重現

```powershell
# C:/portable/OpenKoikatsu
& Tools/GamePoseProbe/build.ps1
& Tools/GamePoseProbe/run_multilayer_native.ps1 -Cards All
Tools\lua\lua.exe Tools/anim_export/verify_native_multilayer.lua
# C:/portable/OpenKoikatsu/Tools/ok-extract
cargo test --release --lib multilayer -- --ignored --nocapture
# build Unreal後，C:/portable/OpenKoikatsu
& Tools/GamePoseProbe/run_multilayer.ps1 -Mode Tests -MultiLayerOnly
& Tools/GamePoseProbe/run_multilayer.ps1 -Mode Tests -RegressionOnly
& Tools/GamePoseProbe/run_multilayer.ps1 -Mode Shots -Blink open -Cards All
Tools/lua/lua.exe Tools/GamePoseProbe/verify_multilayer.lua
```

## 尚未涵蓋

原公開graph未提供的獨立Override state配對、HandCtrl完整自動排程／item動畫、每層獨立起播時間、transition／CrossFade、Directional2D、mirror／footIK、零／負speed、Timing=true、FK／IK、所有mod與shader逐像素等價仍未全部完成。單一非零extra default weight的controller仍不自動初始化。本輪新增多選擇描述，既有Studio目錄不會自動猜哪些層該同時啟用。

## 驗證結果

五卡正式Rust兩項101.90秒PASS：native75frames／20250 TRS attrs（每frame完整90個來源CF骨×3），position／rotation／scale max均1.1920929e-7；每frame倒序描述選擇與正序描述結果逐值相等，仍依來源index處理。duplicate layer、unknown layer／base、空集合與nested descriptor共25項來源拒絕。

Session75frames／45000完整骨TRS／1627365 runtime private附件頂點，max骨5.9604645e-8、附件0m；各選定層負值／>1／NaN／Infinity皆拒絕，參數與時間不變，physics保持開啟。此Session對比與source evaluator共用取樣數學；獨立原生證據另外保存。

UnityPy独立以原生base＋isolated layers及實際來源path mask重建34995個Transform snapshots：max position6.15e-9、rotation1.0182802e-7、scale4.342e-8；reverse layer order最大誤差.00027011127（2e-5門檻外）。來源5個layer modes7/8/70=Additive1、73/74=Override0。

production staticlib／Unreal build25.67秒Succeeded。UE新test五卡75frames全部Success、QueueEmpty1、exit0；實際published palette每卡1骨隨layer8 weight0→1改變（共5），拒絕390次非法權重。這5骨palette變化與raw90骨原生驗證用途不同。

RHI五卡15PNG900×1080，71個attachment audit max0cm、15 fits≤.92、424 follower samples、bone→palette max2.252e-6cm；五卡face time／gaze／highlight／EyeLook／source Auto scene gates齊全。已目視完整ContactSheet及March全身／正臉、Alice／KK384正臉原圖，體型、眉眼、側髮、髮飾與頸飾未見新增偏移。這是geometry regression review，不宣稱shader pixel等價；PV display只檢查binding／palette／followers，source H不执行Studio PV。Root Stage286 T-pose15圖SHA256不變，新capture資料夾只含這次15PNG＋README。

最終七項distinct Unreal tests Success：新MultiLayer75frames＋1D450／2D300／單層Additive180／motion time scale300／clock180／Studio initialization135，共1620 source frames。QueueEmpty分別1／6，兩個程序均exit0。`verify_multilayer.py`完整PASS，`Saved/Logs/AnimatorMultiLayer298Audit.json`／`AnimatorMultiLayer298Verified.log`保存source bundles／native fixtures／code／binary／測試logs／PNG SHA256。native probe已移回Tools；本輪owned native與UE均退出，未保存遊戲卡／scene／配置。既有CommonUI viewport routing Error／resource cleanup hitches仍保留；不能概括成完整log無Error。


## Stage299後續

每層獨立epoch／normalized初始phase，以及Unreal未wrap elapsed已接同一取樣器，詳見`UNREAL_ANIMATOR_LAYER_CLOCK.md`；前述Stage298缺口列表是當時狀態。Transition／HandCtrl trigger排程仍未實作。

Stage300補選定層來源瞬時trigger self re-entry，詳見`UNREAL_ANIMATOR_TRIGGER_REENTRY.md`；完整transition／HandCtrl scheduler仍待。
