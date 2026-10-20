# 3800 構圖：鏡頭參數核對與頸部 FK 修正

2026-10-21。實作 `304d10724`。

使用者回報 `koikatu_cs0003800` 鏡頭參數不對。來源與執行中相機核對後，位置、含 roll 的旋轉及 FOV 都正確；此次找到的姿勢差異是前次加入的 FIX 頭部處理覆蓋了此場景啟用的頸部 FK。此次依原作控制語義修正骨頭的控制優先順序，保留正確鏡頭資料。

## 實際前後圖

上排修正前、下排修正後；每排左側原作縮圖、右側 Unreal 實際擷取。

![3800 構圖修正前後](images/OKStudio/Camera3800_20261021/comparison.png)

[修正後原尺寸圖](images/OKStudio/Camera3800_20261021/after.png)；[修正前圖](images/OKStudio/Camera3800_20261021/before.png)；[PNG 尺寸與 SHA-256](images/OKStudio/Camera3800_20261021/manifest.json)。公開圖片為實際渲染，檢查完整解碼、IEND 與無卡片 payload。

## 鏡頭證據

| 資料 | 保存／實際值 |
|---|---|
| Unity target position（m） | `[1.161135912, 0.574006855, -26.056299210]` |
| Unity Euler（deg） | `[2.936013222, 6.056291580, 55.250511169]` |
| Unity orbit distance（m） | `[0, 0, -0.024583379]` |
| Unity vertical FOV（deg） | `27.347341537` |
| Unreal horizontal FOV at 16:9（deg） | `46.777942657` |
| Unreal camera position（cm） | `[-115.854563, -2608.071329, 57.526603]` |
| Unreal pitch／yaw／roll（deg） | `[-2.936011, 96.056293, -55.250511]` |

原作 `CameraControl.CameraData` version 2 保存完整 pos／rotate／distance／parse，`CameraControl` 依 `Quaternion.Euler(rotate) * distance + pos` 設相機。Unreal 的位置按來源座標基底與公尺轉公分換算；兩種 FOV 的差別依 `horizontal = 2 atan(tan(vertical/2) * aspect)` 換算。新增比較工具的相機 trace，將實際 view transform／horizontal FOV／aspect／active id 寫入驗收日誌。

實際重拍前後的 camera trace 完全一致；以獨立矩陣計算來源 `Ry * Rx * Rz`、orbit offset 與座標基底換算，位置誤差小於 `0.00001 cm`、FOV 誤差小於 `0.00001°`。機器核對存於 `Saved/OKStudio/3800-camera-audit.json`。

## 根因與通用修正

- 此角色 object 37 啟用 FK，`activeFK[1]`（Neck）為 true，頸部保存 yaw `22.899248°`；頭部 Euler 為 `[4.680644, 8.702961, 342.851410]`。同時 `neckLookPtn=4`、保存的 FIX neck／head locals 都是 identity。
- 原作 `OCIChar.ActiveFKGroup(Neck)` 啟用時會切到 FIX，以停止頸部追視；`FKCtrl.TargetInfo.Update` 仍依啟用的目標寫入骨頭 localEulerAngles。不能把 FIX 解讀成無條件覆蓋所有頸／頭 FK。
- `HeadLook::apply_preserving_fk` 在套用保存 FIX 時，逐骨跳過目前有明確 FK 控制的頸／頭；未接管的骨頭仍套保存姿勢。一般更新及 `apply_shape`／`apply_shape_live` 共用同一規則；清除 FK 後 FIX 自然接回。未依場景編號、角色或相機角度寫特例。
- 沒有修改相機匯入轉換、FOV、相機／角色 root 或光照來配合截圖。

## 驗收

- Editor／Game Development 建置成功；收尾時合併遠端街頭導航修正，再對合併版本建置。
- Rust 419 PASS、467 ignored。新增一般測試驗證完整 FK、單骨 FK 與清除 FK 時各骨頭的控制；ignored 不計入一般 PASS。
- `scene_3800_neck_fk_overrides_saved_fix` 具名來源測試 PASS：核對來源 camera FOV／roll、頸部 FK 開關與來源旋轉；一般 pose、兩條身形重建的實際骨架 quaternions 與來源一致；清除 FK 後恢復保存 FIX。
- 最新 binary 的 6331／C002 手腕與保存頸部回歸 PASS（885 filtered）；較早在新 binary 編譯前啟動的結果不列為最新版本驗收。
- 合併版本 `5cd6fe09c`：Studio GameSelfTest PASS，實際 Actor 的 FIX → Animation → FIX 回歸仍正常；7 項姿勢／動畫接點／材質／陰影 Automation Success；3800 與 6331 同程序重拍 `2 / 2`。實際圖片確認 3800 頭部 FK 方向恢復、6331 保存的固定低頭保留。
- 相機位置／FOV／朝向基底以獨立矩陣核對，修正前後 trace 完全相同。此次沒有修改基礎庫、gamefs 或卡片格式。

本機證據：`Saved/OKStudio/3800-*.log`、`3800-source.json`、`Verify-Camera3800Before/`、`Verify-Camera3800Diag/`、`Verify-Camera3800Accepted/`、`Verify-Camera3800Game/`、`Verify-Camera3800Automation/`。過程錯誤與修正保留於 `Docs/ERROR_LOG.md`。

## 邊界

此修正作用於執行中的共用姿態流程，原有 `.okscene` 已保存頸／頭 FK 與 FIX 資料時不必重匯；較舊版本未保存 FIX 資料的場景，仍應重新匯入原作 PNG。畫面中的光照、色調、髮材質與表情影格仍有差異，未宣稱逐像素相同或所有任意 rig 已支援。
