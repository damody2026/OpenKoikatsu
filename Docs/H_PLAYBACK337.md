# Stage337 — 原生自動初始化與共用 FinalIK 修正

狀態：本輪驗收完成。

本輪接續 Stage336 的 controlled oracle 缺口。原生 SolverManager 自然初始化一次；跨 khh/khs、倒回時間與非均勻父 scale 不重新 Initiate。位置門檻仍為 0.1 mm，quaternion 1-minus-abs-dot 仍為 1e-5，沒有按卡片修補或放寬門檻。

## 根因與修正

1. 原匯入器因動畫切換或時間倒回重建 solver；改為 per-session 保留 solver，從 body-bone prefab 的 joint rest 與原生 cf_n_height 初始化。NativeAuto337R12 男女初始化計數各1，first/latest相同。
2. Effector.ResetOffset 誤清 node.solver_position；改清 node.offset，消除 ReadPose 根部約0.034 mm跨幀誤差。
3. FBIKChain.Stage2 誤在每條 leaf limb 重跑 root chain[0] 的 child constraints，額外移動其他肢體根節點。按 FullBodyBiped 根chain的約束 ownership執行；新增 limb Stage2不改 sibling roots 回歸。
4. IK世界座標與旋轉按原生Transform流程組合：逐層local TRS TransformPoint、translation最後相加、leaf-first parent*rotation；避免矩陣先乘後取position，以及parent鏈quaternion分组引起的初始化彎曲方向偏差。renderer一般pose路徑不改。
5. 區分 installed Mono 管理程式表達式與 Unity原生C++函式。保留原生float local store／函式邊界的捨入，移植managed向量長度、內積／外積、Lerp、quaternion乘法／向量旋轉及三角公式；native C++ OrthoNormalize、LookRotation、FromTo與Transform組合保留獨立f32路徑。
6. 對齊compiled FromToRotation正向平行cutoff、LookRotation共線fallback、Quaternion.Lerp normalized linear interpolation；Bend rotationOffset按C#左結合(qFrom*offset)*vector執行。

Root chain ownership是反編譯 FullBodyBiped 的共用流程，不是針對 t=0.61或某張卡的補償。所有原始FAIL保留，詳ERROR_LOG E-215。

## 原生對照

自然初始化：NativeAuto337R7、fresh NativeAuto337R12；controlled回歸：NativeSolver336R4。兩類motion × Idle/SLoop/OLoop與nonuniform父scale Idle，共8序列96角色幀／每次15360 cf_j骨骼幀。所有角色使用同一求解前target snapshot與FrameCorrect First/Second/Last。

| 最新對照 | solver最大位置差 | complete adapter最大位置差 | 結果 |
|---|---:|---:|---|
| 自然一次初始化／跨motion及倒回 | 0.096883 mm | 0.096826 mm | 96幀PASS／384contacts |
| 每序列顯式初始化（Stage336回歸） | 0.099035 mm | 0.099057 mm | 96幀PASS／384contacts |

quaternion誤差最大2.38419e-7，仍小於1e-5。結果有數值誤差，不能寫成完整solver bitwise max0。

Compiled primitive證據：1176筆OrthoNormalize normal/tangent逐bit相同；1920筆三角z/y及給定原生look的q*v逐bit相同；1920筆managed q*v與384筆骨長對照差0。LookRotation itself component仍有最大1.19209e-7差異，保留既有component1e-5界線，並報告完整輸出差2.12831e-7 m；不將這個差異隱藏為三角核心差0。69,600筆Transform旋轉leaf-first平均component差1.74353e-8。

NativeAutoFinalR3整suite11測試PASS（含1個僅產生診斷的Slerp比較，不能當所有Slerp分支驗收）；FinalIK基本及leaf sibling回歸8項PASS；controlled2項PASS；8 pairs ×4 states ×4 times，共128幀／256active contacts／同clock及height／finite PASS。R9的384筆ClampDirection均changed=false，否定這輪殘差來自Slerp，沒有按猜測改它。

## 部署與介面

HPlayback337UEBuild Succeeded，31.42秒，重新連結最新 ok_extract.lib與KKUnityBundle DLL。部署C ABI四卡96動作5856幀：duration/loop/body_changed/finite/API均PASS；61配對幀、4錯誤輸入拒絕且姿勢保持PASS。四卡實際Gallery角色來源／暫停／重播／配對／速度／結束驗證PASS；fresh Saved/Tests/HPlayback337Gallery四張900×1080 PNG逐張目視，雙方存在且無灰屏。

Unreal Animator1DBlends、Animator2DLayers、AnimatorMotionTimeScale：本輪重新執行，三項Success、正常exit0。

驗收保存至 Saved/Tests/HPlayback337/Acceptance.json，綁定最新source/binary/fixtures/logs/影像SHA。原生探針已撤出，owned CharaStudio及Gallery均結束；Unreal回歸owned PID60916亦正常結束。

## 重現與界線

```powershell
Tools/GamePoseProbe/build.ps1
Tools/GamePoseProbe/run_hpair335_native.ps1 -Tag <fresh> -Solver -AutoInit
# Rust測試工作目錄：Tools/ok-extract；以OK_H_SOLVER_FIXTURE/REPORT、OK_H_ADAPTER_FIXTURE/REPORT指定fresh fixtures與結果。
cargo test --release h_playback::tests::native_ -- --ignored --nocapture
cargo test --release finalik::tests -- --nocapture
Tools/GamePoseProbe/run_hplayback335_gallery.ps1 -Tag <fresh> -AllCards
python Tools/GamePoseProbe/audit_hplayback337.py
```

自然solver初始化已測，但仍使用記錄的native動畫輸入與受控processor呼叫；不等於完整HScene scheduler、所有transition／mod／身形／參數、三人與聲音已移植。Stage334既有renderer限制仍在，配對圖不同主角角色的screen shadow差異亦不是逐像素parity。正式Saved/Screenshots未覆寫。獨立portable/shared-pack的來源改動保留，本輪不改其schema或cache layout。
