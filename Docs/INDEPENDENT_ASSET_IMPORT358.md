# 獨立卡匯入 Unreal 原生資源

> 最新 Stage361 已保留小位移 morph，使用 `Stage361AllR1`；重開驗收請加 `-VerifyMorphs`，見 [INDEPENDENT_ASSET_MORPH361.md](INDEPENDENT_ASSET_MORPH361.md)。

> 最新 Stage360 已另修正骨架頂點色與預設 50% LOD 縮減；請使用 `Stage360AllR3`，見 [INDEPENDENT_ASSET_SKIN360.md](INDEPENDENT_ASSET_SKIN360.md)。以下為歷史驗證與功能說明。

> 2026-10-02 後續修正：Stage358 只保存貼圖首層，Stage359 已保存完整來源 mip 鏈。最新驗收資產請使用 `Stage359AllR1`；以下 Stage358 結果為保留的歷史紀錄。操作與驗證見 [INDEPENDENT_ASSET_MIPS359.md](INDEPENDENT_ASSET_MIPS359.md)。

本功能由 Unreal Editor commandlet 執行，輸出 `.uasset`，不只是預覽 Actor 的暫存網格。匯入使用現有 APNG／獨立卡的空白 GameRoot 載入路徑，不掃描或回退到 KKS 安裝目錄。

## 輸入條件

接受已轉換的 OKPV 獨立 PNG／APNG。共用型卡另需要程式附帶的 `Content/IndependentResources/v1` 資源包；卡內完整快照沿用原有獨立載入流程。不需要安裝 KKS。匯入後 `.uasset` 的使用也不需要該資源包。尚未轉換的原始 KKS PNG 只有角色參數與預覽圖片，缺少外部模型／貼圖，不能憑空產生完整 Unreal 資源；匯入器會拒絕，而非使用遊戲目錄補資料。

參考 [APNG_CARDS.md](APNG_CARDS.md)、[APNG_ALL_OUTFITS.md](APNG_ALL_OUTFITS.md)、[BUILTIN_RESOURCES.md](BUILTIN_RESOURCES.md)、[INDEPENDENT_CARD_PLAN.md](INDEPENDENT_CARD_PLAN.md)。

## 執行

先建置 `OpenKoikatsuEditor`，關閉使用同一組 DLL 的 Editor，再執行：

```powershell
& D:/UE5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe `
  C:/portable/OpenKoikatsu/OpenKoikatsu.uproject `
  -run=KKImportIndependentCard `
  -Card="C:/portable/OpenKoikatsu/Saved/IndependentCards/Release/KK_346095_all_outfits_skinned_apng.png" `
  -Destination=/Game/IndependentImports/MyCharacter `
  -unattended -nosound -NullRHI
```

預設匯入所有服裝；`-Outfit=0` 只匯入第 0 套。目的地必須是尚未存在的 `/Game/` 資料夾，不覆寫卡片或既有資產。失敗會傳回非零碼；資源先存、角色清單最後存。磁碟儲存中途失敗可能留下部分新資源，應保留診斷並改用新的輸出資料夾重試。

輸出 `Character.Character` 為 `UKKImportedCharacter` Data Asset，含來源卡 SHA-1、服裝索引、部件角色／Transform 及模型／骨架的硬參照；`Resources` 內包含：

- 每個可見部件的 Static Mesh，保存目前卡片造型與各材質 section，包括現有預覽的疊加／輪廓 section、頂點色與四組 UV。
- 每套服裝的共用 Skeleton；具有來源 rest mesh／共用骨架索引／權重（kind 1–3）的部件另輸出 Skeletal Mesh，保留來源形變屬性，建立可重建的 MeshDescription 與匯入 LOD。已烘焙或私有骨架部件目前保存為 Static Mesh，不宣稱輸出完整可動畫角色。
- Material、Material Instance Constant 與有 Texture Source 的 Texture2D。貼圖依內容與取樣／壓縮設定去重。

在 Editor 放置 `KKImportedCharacterActor`，把 `Character` 指向 Data Asset，設定 `Outfit` 即可組裝該服裝。此 Actor 只讀 Unreal 資產，不呼叫卡片解析器、Rust session 或 KKS 資源。Data Asset／Actor 位於獨立 runtime 插件 `Plugins/KKImportedAssets`，只依賴 Core、CoreUObject、Engine；移轉到其他專案時帶入此小型插件即可，不需要 KKUnityBundle 或原遊戲解析器。組裝預設使用保存造型的 Static Mesh；Skeletal Mesh 是另行供 Unreal 動畫／製作工具使用的資源，不表示已匯出原遊戲完整動畫、動態骨骼、嘴型控制器或自動組裝動畫角色。

Blueprint `SpawnActor` 可指定 `Character`／`Outfit`；執行期間改動屬性後呼叫 `RebuildCharacter` 更新服裝。Static Mesh 停用預設 lightmap UV 重包，保留 shader 使用的四組來源 UV；驗證會比對來源與實際繪圖的 UV tuples。

角色硬參照可讓 Cook 收集相依資源；專案仍需透過關卡參照此 Data Asset，或明確設定 Cook 目錄／Asset Manager。移轉資源到其他 Unreal 專案時，需攜帶資源相依項，以及 Data Asset／Actor 的類別實作；這不是任意引擎版本的通用交換格式。

## 驗證重開

```powershell
& D:/UE5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe `
  C:/portable/OpenKoikatsu/OpenKoikatsu.uproject `
  -run=KKImportIndependentCard `
  -Verify=/Game/IndependentImports/MyCharacter/Character.Character `
  -unattended -nosound
```

Verify 不接受也不讀取來源卡，檢查各套模型來源幾何／繪圖資料、骨架、權重與骨索引、材質相依、貼圖來源、材質編譯错误，以及重複重建 Actor 不累積部件。一般資源測試可加 `-NullRHI`；材質驗證須使用實際 RHI。

## 外觀與範圍

材質沿用目前 Unreal 預覽的 shader 翻譯，不能把「資源重開／Cook 成功」當作已達到所有卡片的原遊戲逐像素一致。預覽的場景深度 Render Target 不能保存為卡片貼圖：匯入器改存遠深度的中性貼圖，目的場景需自行提供即時陰影 capture，原卡的顏色／alpha／normal 等素材照常保存。沒有加入卡片名稱、角色名稱或每張圖的特殊補償。

本輪驗證產物與錯誤紀錄位於 `Saved/Tests/AssetImport358`、`Saved/Logs/AssetImport358*.log` 與 ERROR_LOG E-236。早期失敗的輸出保留作診斷，不應拿來使用；最終驗證資料夾會在本文件末尾記載。

## 2026-10-02 驗證結果

最終資源位於 `Content/IndependentImports/Stage358AllR4`，角色資產為 `/Game/IndependentImports/Stage358AllR4/Character.Character`。來源是 `KK_346095_all_outfits_skinned_apng.png`，九套均以 `Session.Open("", card, outfit)` 匯入。

| 項目 | 結果 |
|---|---:|
| 服裝 | 9 |
| Static Mesh | 367 |
| Skeletal Mesh | 99 |
| Skeleton | 9 |
| Material／Material Instance | 979 |
| Texture2D | 212 |
| Character Data Asset | 1 |
| 總 `.uasset` | 1,667 |
| Static Mesh 來源三角形 | 1,201,500 |

Editor BuildR19、ImportAllR4、VerifyAllR4 均 actual exit0。VerifyAllR4 為另啟新程序、實際 RHI，未提供來源卡或 GameRoot，包含各套重複 Actor construction、材質來源／相依、Texture Source、skin position／weight／骨索引與材質編譯檢查。不是逐像素外觀驗收。

Windows CookR3 以 `-package=/Game/IndependentImports/Stage358AllR4/Character -CookSkipRequests -SkipZenStore` 只從角色清單收集相依項；actual exit0，Packages Cooked 1,667，Incrementally Skipped 0、Skipped by Platform 0。Cook 結果位於 `Saved/Tests/AssetImport358/CookR3/OpenKoikatsu/Content/IndependentImports/Stage358AllR4`。這證明資源可 Cook，尚未把本次 Cook 產物做成獨立可執行安裝包。

Cook 摘要為 0 errors／5 warnings，全部是既有 Editor 插件的 HTTP 授權／設定請求逾時，無材質 shader 或資源儲存錯誤。早期完整快照 `KK_398530_portable.png` 亦以同一流程 ImportLegacyR3／VerifyLegacyR3 actual0，57 Static／1 Skeletal／158 材質／91 貼圖，四 UV 與重開構建通過；舊卡未保留的頭部權重不會被憑空補回。

RejectExistingR1、RejectRawR1 均收到預期的 actual exit1：前者拒絕既有輸出，後者回報 `PNG has no portable snapshot`，沒有建立 `Stage358RawRejected` 輸出。原始 KKS PNG 的完整匯入仍需要足夠來源資源，這一版不支援把未轉換的任意 KKS 卡直接還原。

隔離專案 `Saved/Tests/AssetImport358/DetachedR2` 只帶 `KKImportedAssets` 與上述 `.uasset`，沒有來源卡、共用資源包、Rust 工具或 KKUnityBundle。DetachedProject 實際 automation Success／exit0，確認解析器 module 未載入、資源包不存在，九套 367 模型／99 骨架模型能重開及反覆組裝。這比單一專案內的 Verify 更直接驗證資源獨立性。

此測試也檢查 Actor 的平移、旋轉與非均勻縮放，所有部件仍附著 Root 且依保存的相對 Transform 移動。`Saved/Tests/AssetImport358/Audit.json` 封存最終源碼、輸入、原生資產、Cook 與 runtime 插件建置結果，保留每輪失敗紀錄。

`RuntimePluginR4` 的 BuildPlugin 在空白 HostProject 通過 UnrealGame Win64 Development／Shipping，actual0。完整 `OpenKoikatsu` 遊戲目標 GameBuildR1 則因既有預覽 shader 建立 API／Editor 動畫測試未隔離而 exit6，沒有宣稱整個工具的 Shipping 遊戲已可建置；runtime 資產類別特別分離以免依賴此路徑。

早期 `Stage358R1/R2/R3` 含失敗診斷產物，`Stage358AllR1` 是分離 runtime 類別前的資源，`Stage358AllR2/R3` 未關閉 lightmap UV 重包；只使用上述 `Stage358AllR4`。來源及衍生資源均未推送，`Content/IndependentImports/` 已加入 Git ignore。
