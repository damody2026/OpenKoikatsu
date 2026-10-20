# Stage359：獨立資產保留來源 mip 鏈

> 後續 Stage360 已修正骨架模型自動縮減與頂點色旗標，最新資產為 `Stage360AllR3`，见 [INDEPENDENT_ASSET_SKIN360.md](INDEPENDENT_ASSET_SKIN360.md)。本頁 Stage359 的 mip 驗證保留為歷史紀錄。

匯入器先前只保存 Texture2D 的第 0 層。現有 `OKCreateRuntimeTexture` 已依來源 mip_policy 建立 mip 鏈，包含透明裁切所需的 encoded-byte 取樣與向下取整；匯出後讓 Unreal 重建低層會丟失這項資訊。此次修正共用貼圖持久化流程，不依卡名、材質名稱或圖片調整。

現在逐層檢查尺寸、像素格式與資料長度，保存整條 mip 鏈至 Texture Source。多層使用 `TMGS_LeaveExistingMips`，單層使用 `TMGS_NoMipmaps`。去重鍵包含完整 mip 資料及層數，因此相同首層、不同低層的貼圖不會被誤合併。無效鏈會回報錯誤並停止匯入。

新的最終輸出為 `/Game/IndependentImports/Stage359AllR1/Character.Character`，磁碟位置 `Content/IndependentImports/Stage359AllR1`。Stage358AllR4 保留為歷史結果，新匯入請使用 Stage359；舊資產需要從獨立卡重新匯入新資料夾，不能從只存首層的舊資產推回原始低層。

```powershell
./Tools/run_asset_import359.ps1 -Tag MyImport -Card C:/path/character.png -Destination /Game/IndependentImports/MyCharacter
./Tools/run_asset_import359.ps1 -Tag MyVerify -Verify /Game/IndependentImports/MyCharacter/Character.Character -VerifyNativeMips -FullRhi
```

`-VerifyNativeMips` 是新增的保存策略檢查：匯入資料夾中大於 1×1 的貼圖必須使用保留來源的策略。1×1 不可能有更低層，不因其預設策略判成資訊遺失。一般 `-Verify` 仍可用於歷史資產的原有結構檢查。

## 驗證

全部實際程序退出碼都保存於 `Saved/Tests/AssetImport359`。

| 驗證 | 結果 |
|---|---|
| Editor BuildR4 | Succeeded，actual0，5.21 秒 |
| NativeMips 自動測試 | 相同首層／不同低層不合併；整鏈相同可去重；三層位元組一致；單層不增生；錯尺寸拒絕 |
| ImportAllR1 | 空 GameRoot，9 套／1,667 資產，actual0 |
| VerifyAllR1 | 新程序、實際 RHI；367 Static／99 Skeletal／979 材質／212 貼圖、四 UV 與構建通過；97 條多層 mip 鏈、總 1,156 層；actual0 |
| MipRedR2 | 舊 Stage358AllR4 的 T_00005 仍會重新產生 mip，預期拒絕 actual1 |
| DetachedR2 | 無卡、共用 pack、Rust、KKUnityBundle；九套重建、建立後移動與 stale assembly 清理通過，actual0 |
| CookR1 | Windows，1,667 包全 Cook；0 errors／2 個既有插件 HTTP 逾時 warnings；actual0 |
| 舊版完整快照 ImportLegacyR1／VerifyLegacyR1 | 57 Static／1 Skeletal／158 材質／91 貼圖；21 條多層鏈、294 Source 層；實際 RHI 重開與 mip 策略通過，actual0 |

MipRedR1 最先命中 1×1 中性深度貼圖，這不能作為低層資訊遺失的證據；修正檢查範圍後另存 MipRedR2，兩輪紀錄皆保留。BuildR2 曾與 ImportAllR1 尾段生命週期重疊，兩者 actual0；最終 BuildR3/R4 與後續驗證均串行，往後必須等自有 Editor 程序真正退出再 build。

這次驗證保存的 mip bytes 與使用策略，不等於原作全部 shader／壓縮後逐像素一致。資產仍沿用 Stage358 的範圍：已轉換的 PNG/APNG，共用卡匯入時需要內建資源包；原始未轉換 KKS PNG、完整角色動畫控制器、私有 rig、完整工具 Shipping 及 packaged executable 驗收尚未完成。歷史 Stage355 的 279 個近中心線碰撞樣本仍未通過。

完整結果與輸入／來源／資產／Cook／歷史封存 SHA256 見 `Saved/Tests/AssetImport359/Audit.json`；不改寫 Stage358 與更早的 Audit。
