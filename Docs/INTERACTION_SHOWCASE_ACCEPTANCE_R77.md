# R77 每次更新共用 AssignedWeights world 查詢

## 計畫與實作

同角色同同步pass內，服裝的 AssignedWeights body 骨架 world 查詢原本逐 renderer 重做，名稱索引每次還會驗證整個 inventory。新增 `AssignedWorldFrame`：按實際 private slots 收集名字，使用原 `rig_name_index(false)` 與 `world_of` 計算一次，連缺失名字也保存；joined worker 只借用不可變 map。

只在需要 geometry 時準備，不增加 palette-only dynamics 工作；普通 body 與 coordinated clothes 使用各自的已求解骨架，快取不存 session、不跨幀、不序列化。implanted slots／移除骨骼／缺失 slot 的 fallback 完全保留；未知 inventory 回到原查詢。沒有修改動作時間、physics、眼睛、材質、畫質或水槍。

`OK_ASSIGNED_WORLD_FRAME=1`／run script `-AssignedWorldFrame` 才啟用實驗快取；native及run script預設0。`-LegacyAssignedWorlds`明確回到原查詢。

## 原公式驗證

- `InteractionRustR77Compile.log`：combined skin streams 與獨立 position／normal／tangent 路徑完全一致，PASS。
- `InteractionRustR77Assigned.log`：4,464 exact matrices，位置／法線／切線 exact 相等；六次變動根骨骼位置／旋轉／非等比 scale，coordinated implant missing 與不完整快取 fallback 通過。每次建立的新 map 隨 pose 變動，沒有沿用舊 frame。

## 修改前正常 RHI 基準

`PairProfileR77Baseline`：entry7270.036ms，firstpair243.751ms；quiet 全幀16.593339／23.442753ms、Present60.262490／42.658660。externalUE0／isolation1／四DLL stable，水槍0。量測 completed，但性能失敗且 phase 波動大；不能當作新 binary 的收益證據。

本輪完整驗收結果如下。8ms／Present120FPS仍是實際門檻，不能用 stat fps 畫面或 worker CPU 總和替代。

## 同程序同姿勢 A/B/A
AssignedWorldAB只在PairProfile啟用，不修改一般profile。OFF→ON→OFF均暖機3秒與quiet15秒，第三段也保留8ms／300ms／119.5Present判定；不切換姿勢，不使用verbose日誌。
PairProfileR77AssignedABA：OFF11.582770ms／86.325850FPS、ON12.092381ms／82.701789FPS、OFF11.608001ms／86.137518FPS；Advance7.973884／8.338552／7.983040ms，GPU1.483682／1.491219／1.472242ms。前後OFF接近，ON沒有收益，預設關閉。serial prepare與worker saved成本比例尚未量測，不能宣稱已完全定位回退原因。entry17103.490ms、firstpair385.791ms亦未達門檻。externalUE0／isolation1／四DLL stable，120fps0，水槍0。量測completed不等於功能或性能PASS。
## 最終編譯與回歸
InteractionRustR77Observer.log：222400 vertices、兩種completion與scoped queries／snapshot PASS。InteractionBuildR77Final.log15.33s及預設關閉版InteractionBuildR77Default.log41.81s均四模組Result Succeeded。中途InteractionBuildR77AB.log失敗為另一場景UE_LOG後接else缺大括號，僅补括號修正並記入ERRORS.md。

## 最終完整功能驗收
PairEyesSpeedR77Final functional_pass1：entry6878.527ms、firstpair72.562ms；12準備姿勢switch max click23.479ms／max frame49.530ms。13速度phase全原CPU／frame門檻PASS，68callbacks max0.069402ms；54嘴型、語音、walking loop兩輪、shared clock／pause、eyes／head／belly、male replacement failure preservation、return/reentry與shadow oracle通過。五張眼睛原PNG逐張人工檢查正常，eye-visual-review.json保存；startup stat fps enabled1與右上角overlay確認。水槍SkipPaint，externalUE0／isolation1／4DLLstable，無ensure/fatal。
功能採樣平均7.593ms包含不同角色狀態與載入，不能當作穩定雙人quiet性能證據，120fps_present_verified0。
PairRegressionR77Final：13/13 Success，包含external StreamUpload CPU/GPU packing、snapshot、leader／affine、speech／mouth、UI與camera控制。與功能測試及最終profile四個DLL hash 4/4相同。InteractionRustR77AssignedFinal.log重新以OK_ASSIGNED_WORLD_FRAME=1及fixture執行ignored test：4464 exact matrices與所有geometry streams PASS；該opt-in測試需明確設定該環境變數。

## 最終正常 RHI 效能
PairProfileR77Final（預設AssignedWorldFrame0，NativeDisplayStreams0）：entry6615.587ms、firstpair186.926ms；quiet全幀10.878257／10.745831ms、Present91.926537／93.052426，Advance7.486265／7.393287ms，GPU1.467873／1.466388ms，wait0.118767／0.114454ms。externalUE0／isolation1／四DLL stable，120fps_present_verified0，水槍0。phase2詳細日誌13.598874ms不當quiet效能證據。
本輪新增通用scoped query與可靠同程序A/B/A工具、原公式測試；性能實驗未採用，現有功能完整驗收PASS，但8ms／Present120FPS未達成，先前17.1秒載入及385.8毫秒首次提交仍保留為未解決波動。全部本輪build/test/profile已結束，未留下進行中驗收。
