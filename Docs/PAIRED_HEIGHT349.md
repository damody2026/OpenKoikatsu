# Stage349：穿衣雙角色身高差數值驗證

**結論：目前不能認證巨大身高差時整體正常。** 雙人時鐘與骨架數值可推進，但額外極端骨架比例的目標殘差增大；不同 Actor 世界縮放目前直接拒絕開始。衣服穿插與原生極端比例逐幀對照沒有驗收。

## 實際測試

來源 KK_226176 女卡及 KK_224305 男卡，outfit0。兩個來源皆保留 clothes 部件；本次只量測骨架，不產生互動場景圖片。

原生 AnimationInfoData 選出兩類共 4 組配對、8 個共同 state，全部具有實際啟用的 MotionIK 位置目標。5 種配置 × 每 state 9 個時間點，合計 360 雙人幀／720 角色幀／2880 active-effector observations。時鐘與共同 height 參數一致，非有限值 0，暫時 proxy 骨骼均清除。

| 配置 | 女／男 cf_n_height scale | 最大 IK 目標殘差 |
|---|---|---|
| 身高參數皆 0.5 | 0.92／0.92 | 0.669 mm |
| 原生身高 0／1 | 0.84／1.00 | 0.611 mm |
| 原生身高 1／0 | 1.00／0.84 | 0.727 mm |
| 額外骨架倍率 0.6／1.8 | 0.504／1.80 | 6.977 mm |
| 額外骨架倍率 1.8／0.6 | 1.80／0.504 | 114.419 mm |

原生身高參數兩端只提供約 1.19 倍的 height-node 比例差；不能當巨大身高差正控制。額外倍率案例約 3.57 倍，為直接調整 shaped skeleton 的壓力診斷，不認證實際 ABMX/card 匯入或原生遊戲在相同比例下的結果。

殘差是正權重 effector 的目標位置與求解／FrameCorrect 後骨骼位置之差，包含部分權重約束。不是先前 native parity 的 0.1 mm 誤差門檻，也不能把所有殘差都解讀成 solver 移植錯誤。極端配置的較大殘差是不能認證對位正常的證據，仍需原生相同輸入對照以定位原因。

## 世界縮放限制

KKPairedPlayback.Start 要求兩 Actor 的完整 Transform 相等，包含 scale。Unreal `OpenKoikatsu.Pairing.DifferentActorScaleGuard` 實際測試 1×／2×兩 Actor，確認開始即被拒絕，actual exit0。移除此 guard 並不能建立正確的兩角色世界／骨架座標轉換，因此本次保留限制。

## 證據與狀態

Rust R3 數值測試 1 PASS、105.53 秒；UE guard Success／exit0；UE build Succeeded37.20秒。`Saved/Tests/PairedHeight349/report.json` 保留逐案資料，`Acceptance.json` 封存來源、兩張卡、LIB/DLL與日誌 SHA。原始 report 的 `all_scenarios_normal=false` 表示未認證正常，並非本次 numeric smoke 的自動失敗條件。

本次新增驗證工具，未修改產品播放或求解邏輯，SESSION239／CLI349。測試失敗與無效零接觸抽樣記入 ERROR_LOG E-227。既有 Stage335–337 為先前配對實作證據，不能代替本次極端身高验收。
