# Stage350：通用 IK 縮放運算順序

共用 IK 世界座標路徑已修正：先縮放局部點，再旋轉及平移。先前將 scale 乘入 TRS 矩陣欄位，會改變浮點捨入；接近伸直的腿會將約 0.000112 mm 的腳踝輸入差異放大為 1.414 mm 的膝部輸出差異。本次沒有卡名、動作或時間點補償，沒有修改普通 renderer 的 pose 路徑，也沒有更改原生 IK 目標或權重。

SESSION240／CLI350／CACHE108，persistent schema 不變。修改位置為 `Tools/ok-extract/src/finalik.rs` 的 `IkSkeleton.world_pos`；反變換原本已採 inverse rotation 後除以 scale。原生反編譯依據 FBIKChain.ReadPose/CalculateBoneLengths/SolveTrigonometric、IKEffector.isEndEffector；TransformPoint 的浮點運算順序由實際 compiled-native 數值對照定位，不將反編譯 icall 宣稱為完整 Unity C++ 原碼。

## 原生正負控制

原生探針只建立 `p_cf_body_bone` 骨架 prefab，停用所有 Renderer/Behaviour，不讀角色卡、不播放場景動作。比例 0.92/0.92、0.504/1.8、1.8/0.504；每組兩個角色各八幀，保留各 solver 狀態。分別測四個手腳末端及只啟用雙手的情況，全部為一般伸展數值控制。

| 對照 | 角色幀 | 最大位置差 | 結果 |
|---|---:|---:|---|
| 修正前只啟用手部，NativeR3 | 48 | 1.413999 mm | FAIL，四個骨骼幀超出門檻 |
| 修正後只啟用手部，GreenR3 | 48 | 0.007719 mm | PASS |
| 修正後四個末端，GreenR2 | 48 | 0.014488 mm | PASS |
| Stage336 controlled fixture 回歸 | 96 | 0.099035 mm | PASS |
| Stage337 natural initialization fixture 回歸 | 96 | 0.096889 mm | PASS |

位置門檻仍是 0.1 mm，旋轉 quaternion 1-minus-abs-dot 仍是 1e-5，未放寬。八項 FinalIK 回歸通過。`TransformEvaluation.json` 比較五種計算順序，先 scale 點的方式使六個骨架的右脚踝輸入差為零；其餘骨骼仍存在微小原生浮點差，不能宣稱 bitwise 全部相等。NativeR1 因測試工具誤認足端骨骼而只有手部權重，不能列為四末端正控制；此失敗及 NativeR3 的真正手部負控制都保留。

## 縮放限制與未認證範圍

求解器與原生對齊不等於所有目標都可達。四末端伸展的原生最大目標殘差，在相同比例為 39.101 mm，在約 3.57 倍比例的較矮骨架為 113.200 mm；雙手控制為 147.897 mm。這是原生骨骼輸出與設定目標的距離，**不是**移植誤差。這些是一般伸展控制，不能代替 Stage349 場景的同輸入極端身高 oracle，也不能認證衣服穿插、不同 Actor 世界縮放或巨大身高差的整體對位正常。

NativeR2/R3 的完整數值 fixture 已產出，但 Studio 在 Quit 後 15 秒仍存活；runner核對其自有 PID 與 executable 後終止，actual exit=-1，沒有標成乾淨 exit0。probe 已撤出，兩次原生執行 DLL 保存於測試目錄。錯誤記於 ERROR_LOG E-228。

部署建置與額外回歸的最終狀態，以 `Saved/Tests/GenericIkScale350/Audit.json` 及對應日誌為準；此檔只封存實際完成的檢查，不改寫 Stage348/349 的歷史 acceptance。

最終部署：Editor build Succeeded31.77秒、CLI build37.38秒；LIB、CLI EXE及Editor DLL都檢出350版本字串。三項UE Animator1DBlends/Animator2DLayers/AnimatorMotionTimeScale全部Success、actualexit0。重新跑4pair/8state/5配置，共360pair／720role frames，nonfinite0、共同clock/height及proxy清理通過，241.49秒；用新目錄保存，沒有覆寫Stage349。

重測native身高範圍max target residual0.727082mm；額外3.57倍比例max114.418752mm，與Stage349相近，本次精度修正沒有消除此限制。`Audit.json`保存50項source/native assembly/decompile/fixture/report/log/LIB/DLL/EXE SHA，all_generic_ik_cases_passed只指本節所測案例，large_height_difference_scene_normality_verified仍false。全部自有Studio/UE/cargo/test程序結束，沒有場景截圖或外部發布。

重新取樣須先 build `Tools/GamePoseProbe/build.ps1`，再呼叫 `run_generic_ik_scale350.ps1 -Tag <全新標籤>`，雙手控制另加 `-HandsOnly`。runner不覆蓋現有fixture，也不接管其他Studio或probe。Rust以 `OK_H_SOLVER_FIXTURE=<fixture.json.pair-solver.json>`、`OK_H_SOLVER_REPORT=<全新report>` 執行 `cargo test --release --lib native_persistent_solver_matches -- --ignored --nocapture`。身高稽核新增 `OK_PAIR_HEIGHT_AUDIT_OUT`，以新目錄寫結果，避免覆蓋Stage349歷史證據。
