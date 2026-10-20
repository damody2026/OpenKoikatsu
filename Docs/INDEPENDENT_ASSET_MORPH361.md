# Stage361：保存 Skeletal morph 小位移

匯入器前幾輪已保存 MeshDescription 的 morph 位置 delta，但 Unreal 建立可播放的 MorphTarget 時，預設 `MorphThresholdPosition=0.015` 公分會丟棄較小位移。模型、骨架及來源 morph 屬性存在，不代表 render morph 完整。

本輪在共用骨架匯入流程設定 `MorphThresholdPosition=0`，不依角色、卡名、表情或圖片補償。保存前逐一比對 morph 名稱、render 頂點索引映射與位置 delta；新程序重開可用 `-VerifyMorphs` 執行相同檢查。重複或越界的 morph 頂點索引會拒絕。

檢查跟隨 Unreal 的 MeshDescription → 絕對 morph 點 → 相對 delta 計算順序，即 `(Base + Delta) - Base`。這不是任意 epsilon 比對；來源屬性仍保存原始 delta。Unreal 自身 `IsNearlyZero` 的極小 delta 處理另計數，不能冒稱這些位移的 render 原始 bit 一致。此批次該計數為零；尚未做其他卡的非零極小 delta 控制，也沒有驗收 morph normal delta、動畫控制器或原作 GPU 畫面。

## 驗收資產與使用

最新輸出為 `/Game/IndependentImports/Stage361AllR1/Character.Character`，磁碟目錄 `Content/IndependentImports/Stage361AllR1`。Stage360AllR3 的來源 morph 仍在，但部分 render 小位移已被預設門檻刪除，請從獨立卡重新匯入新資料夾；既有資產與封存不覆蓋。

```powershell
./Tools/run_asset_import361.ps1 -Tag MyImport -Card C:/path/character.png -Destination /Game/IndependentImports/MyCharacter
./Tools/run_asset_import361.ps1 -Tag MyVerify -Verify /Game/IndependentImports/MyCharacter/Character.Character -VerifyNativeMips -VerifyMorphs -FullRhi
```

## 已確認的反證與結果

- MorphRedR1：舊 Stage360AllR3 另啟程序檢查，`eye_face.f00_def_cl` 頂點 582 的來源約 Y=-0.001、Z=-0.003 公分，render delta 為零，預期拒絕 actual1。這是實際變形資訊遺失，不能用載入成功或靜態圖正常代替驗收。
- ImportSmokeR1：單套、312 個模型內 morph 項目／96,012 個有效 render delta，actual0。
- ImportAllR1：空 GameRoot，九套共 1,667 個資產，保存前核對 2,808 個模型內 morph 項目／864,108 個有效 render delta，UE nearly-zero 計數 0，actual0。數目為各模型／服裝累計，不代表 2,808 個不同表情。
- VerifyAllR1：另啟新程序、實際 RHI，同樣 2,808 項／864,108 個有效 delta／極小項 0，actual0；99 骨架模型完整 226,512 三角形與所有色彩 buffer 角資料通過，97 條多層 mip 鏈／1,156 Source 層維持。
- DetachedR1：無卡、共用 pack、Rust、KKUnityBundle 的 runtime 資產專案重開／組裝／建立後移動／清理 Success，actual0。
- CookR1：全部 1,667 包 Windows Cook，actual0；0 errors／2 個既有 Editor 插件 HTTP 逾時 warnings。
- NativeMips R1：原 mip 對照測試 Success，actual0。
- ImportLegacyR1／VerifyLegacyR1：舊完整快照 actual0，57 Static／1 Skeletal／158 材質／91 貼圖，骨架 16,754 三角形完整、21 條多層鏈／294 層通過。此舊卡可匯出骨架部分沒有 morph，targets=0／samples=0；這是無 morph 資料的相容性回歸，不能算舊卡表情驗收。

Source 索引配對使用重建 LOD 的公開 `GetRawPointIndices()`，且必須逐頂點確認來源位置等於 render 位置；不能在縮減模型上盲用索引。此次流程沿用 Stage360 的完整 LOD0、位置／UV／顏色與三角形檢查，並保留 Stage359 的 mip 鏈。

結果、退出碼與 SHA256 保存於 `Saved/Tests/AssetImport361/Audit.json`；歷史 Stage360 Audit 保持原檔。第一輪猜錯 Unreal MorphTarget.cpp 路徑，後來以 `rg --files` 定位至 Engine/Private/MorphTarget.cpp；應先定位檔案再讀取。

本功能仍限定已轉換的 PNG/APNG；共享卡匯入時需要內建共用資源包。原始任意 KKS 卡還原、完整 controller、私有 rig、原作像素一致、整個工具 Shipping／獨立可執行包尚未驗收；Stage355 的 279 個近中心線碰撞案例仍未通過。
