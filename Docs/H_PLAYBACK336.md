# Stage336：原生多幀 IK 求解與配對 adapter

2026-10-01，接續 Stage335 尚未验证的求解結果。修正共用算法及時序，沒有角色名、卡片或圖片專用補丁。完整驗收與 SHA256 在 `Saved/Tests/HPlayback336/Acceptance.json`。

## 共用修正

- `BaseCorrect.Info.type` 的 First／Second／Last 必須保留。原生工廠的手指十項為 Last，其他十四項為 First；adapter依原生資料在求解前後執行，不再全部放求解前。後置修正使用原始 MotionIK state，不能使用為 solver proxy 合成過的 shape 資料。
- 每個 Session 保留獨立 FullBodyBipedIK runtime，正常前進跨幀重用；切換 MotionIK 或時間倒退時重新初始化。runtime 不寫入卡片／snapshot，原有序列化 schema 不變。
- `ChaControl.objBodyBone` 指向 `cf_j_root`，並非外層 prefab `p_cf_body_bone`。效應器與 bend 的 TransformDirection 偏移用正確身體根部四元數。
- 兩角色的 Sync targets 都從同一份求解前、已做前置修正的骨架取樣，避免後解算的一方讀到另一位已求解的骨骼。
- Unity `Transform.rotation` 是父子 local quaternion 的乘積，不能由帶非均勻縮放／shear 的 world matrix 去 scale 後取得。FinalIK 的 world rotation getter／setter改用既有階層四元數算法，涵蓋 ABMX 非均勻縮放。
- Gallery 測試截圖改為每次 fresh tag 的獨立資料夾；未指定目錄時使用 GUID。runner 拒絕已存在的目錄，並檢查 PNG 存在，避免重跑覆蓋歷史證據。

## 原生 oracle

臨時 GamePoseProbe 使用安裝中的原生 `MotionIK.Calc`、`BaseProcess.LateUpdate`、`FullBodyBipedIK.solver.Initiate/Update`。記錄初始骨架、動畫取樣後骨架、前置修正與 target 後的 solver input、solver output、Last 修正後的 output。

`NativeSolver336R4` 含 khh／khs 两類、共同 Idle／SLoop／OLoop，以及兩類額外的非均勻父層縮放 Idle（1.15／0.85／1.05）。每組雙角色取樣 t=0／0.1／0.25／0.61／1／1.25，共8序列、96個角色幀、15,360個 cf_j 骨骼幀。native runner exit0，probe 已撤出，沒有保存／改寫遊戲角色卡。

以相同原生輸入比較兩個層次：

| 項目 | 結果 |
|---|---|
| 持續 solver，直接使用原生 target | 96幀 PASS，最大位置差8.6088097e-5m（0.08609mm） |
| 完整 FrameCorrect／shape／contact／solver adapter | 96幀、384 active contacts PASS，最大位置差8.5875072e-5m（0.08588mm） |
| 兩者最大旋轉差 | quaternion `1-abs(dot)` 為1.7881393e-7 |
| 門檻 | 位置1e-4m，quaternion `1-abs(dot)` 1e-5；沒有放寬失敗後的門檻 |
| 原生綁定／shape 合約與工廠 | 原有合約及新24項執行階段比對 PASS |
| FinalIK 基本回歸 | 7項 PASS |

證據：`Saved/Tests/HPlayback335/NativeSolver336R4.json.pair-solver.json`、`.pair-contract.json`；`Saved/Tests/HPlayback336/Solver336Audit.json`、`Adapter336Audit.json`；`Saved/Logs/HPlayback336NativeAuditFinal.log`、`HPlayback336FinalIkRegression.log`。

完整 adapter 使用相同原生動畫骨架作輸入，從解析的 MotionIK／工廠資料自行計算修正及 targets。這補上了 Stage335 只有 binding／weight／shape oracle 的缺口；它仍不等於整套動畫取樣與原生 H scene 自動生命週期完全一致。

## 部署回歸

最後 UE build、四卡96動作／5,856幀（骨骼隨時間變動、finite、API全PASS）、61配對幀與4負向輸入（拒絕且姿勢不變）、128配對幀／256contacts、四卡實際 Gallery 選取／切來源／暫停／重播／配對／速度／結束，均由驗收 JSON 引用各自結果。最後獨立截圖目錄再對首組男女卡驗證。Stage335 三項 Unreal Animator 回歸及相機／灰屏負例仍是先前基線，不誤標為本輪重跑。

重現：

```powershell
Tools/GamePoseProbe/build.ps1
Tools/GamePoseProbe/run_hpair335_native.ps1 -Tag <fresh> -Solver
# oracle 測試的預設 fixture 為 NativeSolver336R4；solver-only 支援 OK_H_SOLVER_FIXTURE。
cargo test --manifest-path Tools/ok-extract/Cargo.toml --release h_playback::tests::native -- --ignored --nocapture
Tools/GamePoseProbe/run_hplayback335_gallery.ps1 -Tag <fresh> -AllCards
```

## 失敗與界線

E-214保存所有失敗。首次原生骨架含重名 n_cam_tc，改以完整path建立父子identity。首次 Rust fixture 根骨id=0，被Unity PPtr模型當NULL，男角180度根旋轉遺失造成1.27m假差異；修正為非零id後solver通過，不能把測試錯誤說成runtime根因。首次adapter的手部／手指差異在保留原始後置shape資料與四元數階層修正後通過。

早期 Gallery 截圖固定在 Stage335 目錄，重跑前組男女兩張原PNG未備份而被更新；兩張原SHA及舊合圖仍保留，但原全解析度檔案無法恢復。舊合圖、後組兩原PNG已按舊SHA保存到 `Stage335Preserved`；Stage335 Acceptance 明確標為歷史結果，由本輪驗收取代，不用歷史SHA宣稱當前別名相同。

oracle 是控制初始化與 processor 執行階段的原生算法比較，未驗證 Unity 自動 scheduler、實際 HScene 首次初始化、全部 controller／mod／身形、所有自動transition、三人、音效與完整 scene flow。已驗證範圍內有數值誤差，並非逐位元零差異。Stage334既有材質／renderer／Siesta整PNG問題與 CommonUI viewport 提示仍另行保留，不宣稱全專案零bug。
