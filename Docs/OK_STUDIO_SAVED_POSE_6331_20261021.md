# 6331：手部與固定頭部姿勢修正

2026-10-21。核心實作 `5e0e3d0fa`；獨立固定模式 `dc182c862`；整合測試命名修正 `1acae0f60`；身形重建保留 FIX `43aa89925`。

`koikatu_cs0006331` 左側角色（來源 object 555）的手掌 IK 位置原本已接近目標，但手腕方向與低頭姿勢沒有還原。這不是場景相機或角色 root 的位移問題。

## 實際前後圖

上排修正前、下排修正後；每排左側是原作縮圖，右側是 Unreal 實際擷取。

![6331：手部與固定頭部修正前後](images/OKStudio/SavedPose6331_20261021/comparison.png)

[完整修正後圖片](images/OKStudio/SavedPose6331_20261021/after.png)；[修正前圖片](images/OKStudio/SavedPose6331_20261021/before.png)；[尺寸與 SHA-256](images/OKStudio/SavedPose6331_20261021/manifest.json)。圖片完整解碼，確認 IEND 後無資料，沒有 okPv／okLc 卡片 payload。

## 來源證據與通用實作

- 原作 `AddObjectAssist.InitIKTarget` 只讓手、腳目標使用旋轉（target 3、6、9、12）。舊匯入只傳 position，現在同時保留來源 Unity Euler，並以四元數傳入 FullBodyBipedIK。未啟用的部位與舊的純位置 API 保持原行為，清除 IK 時同步清除旋轉。
- 左手來源位置為 `[-0.096444726, 1.017436981, 0.382595062]` 公尺，旋轉為 `[17.893066, 98.327393, 322.824615]` 度。修正後實際骨架仍貼近該位置，不以移動手掌或角色 root 彌補方向差異。
- 原作 `NeckLookControllerVer2.SaveNeckLookCtrl` 保存 pattern、aBones 數量與每個 fixAngle 四元數；Load 同時還原 fixAngle／backup 與骨頭 localRotation。來源 object 555 的 `neckLookPtn=4`，保存兩個局部旋轉；舊解析器直接略過這個資料塊。
- 實際原作 `studio/base/00.unity3d` 的 `NeckLookCalcVer2.aBones` 順序確認為 `cf_j_neck`、`cf_j_head`，controlBone 均為空。依此原生 rig 契約保存與套用，沒有依場景 ID 寫特例。
- FIX 在來源動畫、FK／IK 之後、動態骨之前套用。固定頭部有獨立控制狀態，不借用 FK，避免改變全身 PV／FK gate；也不將固定旋轉烘入 shaped baseline。
- 原作固定資料對應獨立的「固定姿勢」模式。有保存資料時可切換固定、照動畫、看鏡頭、看注視點；切離固定會清除 runtime FIX，保留資料可再選回。舊場景仍以既有 Animation／Camera／Point 值載入。
- `apply_shape` 與 `apply_shape_live` 的直接重建路徑也會重套 FIX，不推進追視或物理時鐘；deferred 動畫路徑仍在正常 finishing pass 套用。
- 新 C ABI setter 先驗證完整目標／四元數才提交，拒絕無效資料時保留原姿勢。非 FIX 模式不使用的頸部零四元數不會被誤判為損壞資料。

## 驗收

- Rust：418 PASS、465 ignored；固定頸部資料長度／四元數驗證包含在一般測試。
- 具名來源／runtime 測試：6331 保存 FIX 資料 PASS；C002 真實 OK 卡的手腕世界旋轉、IK 位置、頸／頭局部旋轉、兩種身形重建、拒絕無效目標與清除舊狀態 PASS。
- 已提交核心版本六項 Studio 自測全部 PASS（GameSelfTest、PatternTest、EyeSetTest、BloomTest、TransparencyTest、EditorSelfTest）。透明材質 48 組、101 張 PNG、13 張代表圖通過完整性及 SHA-256 核對。
- 獨立固定模式的最終 GameSelfTest PASS，實際 Actor 驗證固定 → 動畫 → 固定，確認可還原動畫頭部且可選回保存姿勢。
- 6331 與 1268 在同一程序重新匯入／擷取 `2 / 2`，1268 的旋轉父接點仍正常。
- 最後身形保留版本 `43aa89925`：Editor／Game 建置成功；Rust 再次 418 PASS、465 ignored；7 項 Automation Success（姿勢存讀、動畫接點、透明材質與陰影）；6331 實際重新擷取 `1 / 1`，上方修正後圖採用此版本。

本機證據：`Saved/OKStudio/6331-*.log`、`Verify-Pose6331Tracked/`、`Verify-Pose6331FixedModeGame/`、`Verify-Pose6331Release/`；最後版本為 `6331-release-editor-build.log`、`6331-release-game-build.log`、`6331-release-rust-suite.log`、`6331-shape-rotation-oracle.log`、`Verify-Pose6331Automation/console.log` 與 `Verify-Pose6331Accepted/Compare.log`。保留第一輪 Rust 小數語法錯誤與 C4456 撞名 build log，沒有當作通過。

## 邊界

本次修正來源場景的手／腳 IK 方向與原生兩骨 FIX 頸部資料；沒有宣稱任意自訂 rig 或所有追視／表情模式都已完整還原。色調、材質與眨眼影格仍有差異，沒有把整張圖記為逐像素相同。沒有修改相機或角色 root 來對圖。

舊 `.okscene` 未保存這些來源資料，需重新匯入原作 PNG，才能取得手部方向與固定頸部姿勢。
