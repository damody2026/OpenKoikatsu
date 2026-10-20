# R76 Native 蒙皮完成時產生顯示串流

## 通用實作

新增 `ok_animation_pair_frame_observed` additive C ABI。保持原 paired frame 的共享時間、contact barrier與physics；non-shared session在private dynamics產生geometry完成時emit，shared palette-only session則在稍後render_solved消費geometry時emit，兩種路徑各part只emit一次。joined兩角色的Skin；observer只借用至本次blocking call返回，不把callback/context存入Session、快取、clone或背景準備。

GT先驗證完整requests／準備GPU packets，worker更新disjoint canonical／CPU sections、沿用既有normal quaternion／outline／tangent公式，完成exact packing與bounds；join後GT才publish與更新render transforms，後續eyes／morph／shadow保持原時序。missing emit保持原CPU/GPU資料；invalid／duplicate request在native callback之前拒絕。

僅section完整、無morph、非eye／sphere-normal的private parts適用。未處理part仍走原display路徑。當幀NativeSkinReady與part Prepared標記只消費一次，每次pair apply先清除；callback完成時清空source snapshot，避免開關切換殘留舊canonical資料。沒有卡片特化預計算資產，APNG正式入口及共用資源方式保留。

新開關 `ok.Animation.NativeDisplayStreams` 初始預設0；`NativeDisplayParity` 逐值檢查未共用cache的原draw公式。採用前必須同binary正常RHI量測、完整APNG驗收，不能把移動工作直接稱為性能收益。

## 已完成驗證

- `InteractionBuildR76Final.log` 四模組 Result Succeeded，49.84s。首輪C++接線及native test錯誤已記錄MD，沒有放寬門檻。
- `PairRegressionR76` 13/13 PASS（較早R2 binary）；external generated API含串行／平行GPU quantization、UV/color、bounds、missing emit與duplicate rejection。
- `InteractionRustR76ObserverR2.log` native獨立oracle PASS：177,920 vertices的positions／normals／tangents與原render_solved一致；後續一般queries及實際animation snapshot複製不保留callback/context。
- 第一輪 `PairEyesR76NativeParity` 完整場景功能PASS，但沒有新路徑有效part marker；後续定位為只掛render_solved漏掉non-shared dynamics completion。該轮不能當新路徑parity或性能成功，保留fallback功能證據及錯誤記錄。
- 修正版 `InteractionRustR76BothBranches.log` PASS：222,400 vertices，兩個completion分支與原positions／normals／tangents一致，queries／snapshot不保留context。`InteractionBuildR76BothBranches.log` 四模組PASS48.89s。

## 場景與效能驗收

R76原公式parity及完整功能驗收已完成，數據如下。水槍仍SkipPaint；平均≤8ms及實際Present120FPS尚未達成，不標記整體性能目標完成。
## 管線量測與trace結論

修正兩個completion分支後 `PairEyesR76BothParity` 完整功能PASS：actual native display parity 71 parts／102,578 source vertices／126 sections，五眼睛PNG逐張人工檢查正常；entry7077.925ms，firstpair71.145ms，12切換最大click23.794ms／frame36.169ms；54嘴型、語音、walking、eyes/head/belly、male replacement、return/reentry、shadow oracle PASS，無ensure/fatal，externalUE0／4DLL stable。此轮開parity，不作FPS證據。

相同binary normalRHI ON `PairProfileR76Native`：全幀18.152839/16.896371ms、Present55.085040/59.188872；OFF `PairProfileR76Legacy`：9.197234/9.325392ms、108.728496/107.231665。兩輪isolated1／externalUE0／4DLL stable。NativeDisplayStreams沒有採用，預設0。

ON trace `PairProfileR76NativeTrace`：10.197113/11.057101ms、98.065689/90.430688，未重現先前巨大退步，但仍不作正式FPS或收益證據。匯出所有CPU事件、8 native worker IDs78–85，暖機79–82s保留326完整paired frames／652 role samples：GT PairAdvance6.703358、Native5.715148、DynamicsWait3.516226ms；native display callback 71/frame、CPU SUM13.255316ms。後段private draw生成確實0，但工作搬到native critical path；worker CPU sums不能當wall time。證據 Profile-InteractionR76NativeQuiet0-Root.json。

## 直接減少重複計算（Math版）

新增runtime `RuntimeUnitRestNormals`，在唯一RestNormals載入指定點使用原GetSafeNormal精確計算，整個part lifetime不變；缺少cachefallback，parity重算原formula。`RestNormalCache`開關及zero-offset base surface略過identity rotation的`ZeroOffsetRotation`開關，適用原generated path與opt-in native path；不改physics、眼睛、shader、tangent packing或畫質。

`InteractionBuildR76Math.log` 四模組PASS50.69s；此新版另做原公式parity、完整APNG／速度／眼睛／回歸、同binary正常RHI A/B。較早binary結果只作歷史，不代替此版驗收。
Math同binary A/B（兩轮normalRHI）：ON `PairProfileR76Math`全幀11.050756/11.177166ms、Present90.499310/89.452719；OFF `PairProfileR76MathLegacy` 11.369962/11.391301ms、87.949073/87.781611。小幅下降約2–3%，不能由這兩次run斷言穩定性能收益。NativeDisplay仍0，Math開關1；平均8ms／120FPS未達。ONentry9745.492ms，OFFentry13673.216ms（entry門檻失敗，profile只完成量測）。externalUE0不能證明OS負載或頻率穩定，載入波動根因尚未定位，保留原始失敗記錄。

## 最終啟動 FPS 與功能驗收
InteractionBuildR76StatFps.log 四模組Result Succeeded（23.36s）。本地PlayerController BeginPlay使用公開UEngine.SetEngineStat(FPS,true)，明確設為ON；場景重新進入不會反向關閉，不增加每幀控制工作。
PairEyesSpeedR76StatFpsFinal 新程序沒有任何stat fps ExecCmd，startup enabled=1；五張眼睛PNG逐張人工檢查，正確iris／閉眼／重新張眼／播放幾何，右上角全部可見FPS及ms。eye-visual-review.json保存，overlay不替代DXGI Present驗證。
完整functional_pass=1：entry7015.167ms、firstpair88.666ms；12準備姿勢切換max click32.896ms、max frame61.935ms，13速度phases全部原CPU／frame門檻PASS；69callbacks max0.112899ms。54嘴型、語音、walking loop、shared clock／pause、head／mouth／belly、male replacement failure preservation、return/reentry、shadow oracle通過。externalUE0／isolation1／四DLL stable。水槍略過。
此前Math版PairEyesR76MathParity functional_pass1，actual native parity71parts／102578source vertices／126sections；entry7388.100ms，12switch max click24.089ms／frame43.469ms，無ensure/fatal。五圖中Playing被console遮蔽，其餘四圖可驗；最終StatFps版五圖均无遮挡。parity版與最終版差異為啟動FPS設定，不混用binary hash。

最終PairRegressionR76Final：13/13 Success，包括StreamUpload原／generated／external GPU packing、snapshot、leader pose、speech／mouth與UI控制。新版本實際啟動與完整functional／speed均已驗收；startup-only FPS變更未另重跑quiet Present profile，前述Math A/B為效能歷史證據，不能當最終FPS版120證據。性能8ms／120FPS與載入波動仍未解決，無進行中的本輪驗收程序。
