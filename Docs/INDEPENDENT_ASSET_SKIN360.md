# Stage360：骨架模型保存頂點色與完整 LOD0

> 後續 Stage361 修正 morph 小位移被預設門檻刪除，最新資產為 `Stage361AllR1`，見 [INDEPENDENT_ASSET_MORPH361.md](INDEPENDENT_ASSET_MORPH361.md)。本頁 Stage360 資產與驗收為保留的歷史紀錄。

這輪修正獨立卡匯入器的兩個共用問題：

1. 複製 Skeletal render buffer 時，未把 `HasVertexColors` 傳到目的模型。Unreal 的 `FSkeletalMeshLODModel::GetMeshDescription` 會依此旗標決定是否保存顏色，僅複製 `FSoftSkinVertex.Color` 不夠。
2. `AddLODInfo()` 的預設 reduction 設定採 50% 三角形目標。第一次建立 LOD0 時，Unreal 自動簡化幾何、移動頂點，既有驗證只檢查非空模型與有效權重，漏掉了資料遺失。

現在傳遞來源頂點色旗標，並逐角核對轉換後的色彩值。骨架 LOD0 明確使用 100% 三角形與頂點目標，停用退化三角形移除，將位置／法線／UV 合併閾值設為零，保存來源資料而不依卡名或圖片補償。Unreal Skeletal import 使用非 sRGB 的頂點色量化，因此這條流程使用 `ToFColor(false)`；不能直接照搬 Static Mesh 的色彩轉換。

保存前與重開後皆核對全部骨架 LOD0 的三角形數。帶頂點色的模型另核對來源與重建的完整位置、UV、RGBA 組合；重排或相同角資料合併不影響比對，不能只看顏色集合或任意放寬位置誤差。

## 使用

最新資產為 `/Game/IndependentImports/Stage360AllR3/Character.Character`，磁碟資料夾 `Content/IndependentImports/Stage360AllR3`。Stage358/359 的封存與資產仍保留，但其中骨架模型經過預設縮減，應由獨立卡重新匯入新資料夾。Static 預覽組裝本身未經這條 Skeletal reduction，因此不能用預覽圖片正常來認定骨架資產完整。

```powershell
./Tools/run_asset_import360.ps1 -Tag MyImport -Card C:/path/character.png -Destination /Game/IndependentImports/MyCharacter
./Tools/run_asset_import360.ps1 -Tag MyVerify -Verify /Game/IndependentImports/MyCharacter/Character.Character -VerifyNativeMips -FullRhi
```

## 保留的失敗與根因

- `ColorRedR1`：實際匯入因來源頂點色旗標遺失而退出 1，保存前停止，沒有輸出資料夾。
- `ImportAllR1`、`ColorDiagR1/R2`：最初檢查直接把 provenance／raw point 索引當來源描述的頂點編號，位置不符；不能當作顏色資料損壞的證據。
- `ImportAllR2`、`ImportSmokeR1/R2`、`ColorDiagR3/R4`：改用完整來源角組合仍失敗；最近來源角位置平方差約 0.000307946553221 cm²。只關閉合併閾值不會修好，沒有用 epsilon 隱藏差異。
- 根因最後定位為預設 50% reduction。`GeometryRedR1` 在舊 Stage359 重新讀取後實際拒絕：`SK_00014 source=16754 render=8376`。
- `BuildR4` 私有 RawPointIndices2 不可直接讀，需公開 getter；`BuildR6` FColor 沒有可用 GetTypeHash overload，改以 packed ARGB hash。所有 build 失敗均保留，工具的來源時戳檢查拒絕使用舊 DLL。

最終驗收與各實際程序退出碼保存於 `Saved/Tests/AssetImport360`，新 `Audit.json` 保存本輪來源、資產、Cook、輸入與歷史封存 SHA256。

ImportAllR3 空 GameRoot actual0；VerifyAllR3 為另啟新程序、實際 RHI，9 套／367 Static／99 Skeletal／979 材質／212 貼圖，共 1,667 資產 actual0。99 個骨架模型的 LOD0 共 226,512 三角形，來源／重建數量一致；所有重建模型具色彩 buffer 並通過完整角資料比對，原本沒有色彩的部件採預設白色。Static 三角形仍為 1,201,500，四 UV、權重／骨索引／材質與構建通過；97 條多層 mip 鏈／1,156 Source 層維持通過。

DetachedR1 在只有 runtime 插件與資產的專案中 Success／actual0，沒有卡、共用 pack、Rust 或 KKUnityBundle，含重建後移動與清理回歸。CookR1 全部 1,667 包 Windows Cook，actual0、0 errors／2 個既有 Editor 插件 HTTP 逾時 warnings。NativeMips R1 對照測試 Success／actual0。舊版完整快照 ImportLegacyR1／VerifyLegacyR1 亦 actual0，57 Static／1 Skeletal／158 材質／91 貼圖，唯一骨架 LOD0 完整 16,754 三角形；21 條多層鏈、294 Source mip 層及實際 RHI 重開通過。

此次仍限定已轉換 PNG/APNG；共享卡匯入時需要內建共用資源包。完整角色動畫控制器、私有 rig、原始任意 KKS 卡還原、原作 shader 像素一致與整個工具 Shipping／獨立可執行包尚未驗收。Stage355 的 279 个近中心線碰撞案例仍未通過。
