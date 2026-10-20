# APNG 全服裝轉換與驗收

2026-10-02：`ok-preview-pack shared` 已改為轉換原卡全部 outfit。KK_346095 的實際 Coordinate 有 **9 個槽位（0–8）**，全部保留；不以遊戲預設槽位數或先前六套的診斷樣本當上限。

## 頭部身材變形修正

使用者指出 outfit 4 的測試圖脖子斷開、額頭超出頭髮。原始身材 `.3/.8` 正常，改身高 `.222`／胸部 `.444` 後可重現，另存重開也錯。舊 head 烘焙路徑清掉 weights／bone_idx、設 kind 0，臉部只剛性跟隨 `cf_s_head`；髮型則按即時 head tree 變形，兩者不一致。

轉換器 `shared/export` 現在啟用 `OK_HEAD_GPUSKIN`，保留頭部 rest mesh、骨骼權重／索引／bind pose，Unreal 使用 kind 2 骨骼蒙皮。`apply_shape` 也會重算可用的 full-head accessory tree，GPU export pose 可更新只存在於 full tree 的髮型節點。

小樣 `Saved/APNGCards/NeckHairCompare/HeadGpuEdited/view1.png` 已在同樣 `.222/.444` 下確認額頭與衣領錯位消失，原始身材樣本也正確；body 不透明控制證實脖子幾何仍在。最終交付採 `KK_346095_all_outfits_skinned_apng.png`。舊卡的頭部權重已被移除，需從原卡重轉，不能只重拍舊卡預覽。

## 使用與產物

在 Gallery 開啟 `Saved/IndependentCards/Release/KK_346095_all_outfits_skinned_apng.png`，載入 3D 後用服裝選單或前後按鈕切換九套服裝。每套都包含自己的服裝、配件、模型狀態、材質與貼圖覆寫。調整身材後切換服裝會沿用卡片目前的身材值。

按「另存 APNG 新卡（四視圖）」會重新擷取**目前選取的服裝**，同一張輸出卡仍保留其他八套完整 3D 資料。預覽維持臉部 2 秒、正面／側面／背面各 1 秒，768×1024、無限循環。四張預覽不是九套服裝輪播。

| 產物 | bytes |
|---|---:|
| 全服裝靜態新卡 `KK_346095_all_outfits_skinned.png` | 22,154,277 |
| 全服裝 APNG `KK_346095_all_outfits_skinned_apng.png` | 25,166,756 |
| 全服裝私有資源區塊 `okPv` | 21,834,148 |
| 內建共用資源檔案（1,691 個，另加 catalog） | 99,227,777 |
| 安裝包 `OpenKoikatsu_BuiltinResources_v3_HeadSkin.zip` | 83,183,744 |

資源安裝包解壓至程式根目錄，建立／擴充 `Content/IndependentResources/v1`。安裝包的 v3 是本次盤點版本，執行時資源 schema／目錄仍是 v1。此包涵蓋本卡全部服裝所需資源及前次已匯入的內容，保留舊卡所需資源；並非整個遊戲與所有 mod 的全集。未安裝遊戲也能讀取新卡；轉換這次來源資源時使用了來源遊戲與 zipmod。

## 封裝與相容性

`okPv` 使用內部 `OKPV0003`：32-byte header、LZ4 壓縮 outfit 目錄、依 SHA-256 索引的獨有資源資料區。每套保存資源引用和角色狀態；相同獨有資源跨 outfit 只存一份。本卡全九套合計 111 個獨有資源，原始內容 21,550,295 bytes。

共用模型、骨架、來源材質和貼圖留在程式資源包。若某資源於另一套被判定為共用，封裝會移除其卡內副本。載入時只解壓選取 outfit 所引用的獨有資源與共用檔案；PNG 讀取及整體 `okPv` CRC 仍會檢查完整卡片，因此不是完全只讀取所選服裝的磁碟區段。

IEND 後保留 Custom、Parameter、完整 Coordinate；不再重複附加 KKEx 大型貼圖字典。APNG 另存沿用全部 outfit 的資源區塊及 Coordinate，只更新預覽、名稱和可編輯身材。舊 OKPV0001/2 單服裝卡仍只能載入 outfit 0；補齊其他服裝必須由原卡重新轉換。

這仍是專案內部快照格式，不是已承諾跨版本長期相容的公開交換格式。Unreal 材質外觀沿用現有實作；來源遊戲視覺一致性由另一個工作項目持續處理。

## 驗證資料

- `Saved/MultiOutfit-skinned-conversion.json`：原卡九套完整抽取、各套部件數、共用引用及全卡去重統計；轉換耗時 411.33 秒。
- `Saved/MultiOutfit-skinned-verify.json`：九套均在空白 GameRoot 載入，身高、胸部、腰部在 0／0.5／1 的幾何變化有限且非零。
- `Saved/MultiOutfit-skinned-UI.log`：真正呼叫 Gallery 服裝切換按鈕，九套各完成四視圖，最後一套呼叫 APNG 匯出按鈕；另存名稱、身高 0.222、胸部 0.444 後，逐套以空白 GameRoot 重開成功。outfit 4 額頭／衣領錯位已目視消除。
- `Saved/APNGCards/AllOutfitViews/outfit0_view0.png` 至 `outfit8_view3.png`：36 張 Unreal 擷取。
- `Saved/MultiOutfit-skinned-verification.json`：所有 PNG chunk CRC、四幀尺寸／時間／循環、靜態卡到 APNG／身材另存卡的全部 `okPv` bytes 不變、內建資源 catalog 與 zip 完整性。
- `Saved/MultiOutfit-skinned-native-roundtrip.log`：逐套比較來源與獨立卡的模型、rest 頂點、UV、索引、權重、主貼圖、normal、alpha、shader／runtime 貼圖與來源材質，以及身高變形；全九套通過。
- `Saved/MultiOutfitSkinnedReport/index.json`：UE APNG／全 outfit／卡片／zipmod 自動測試 14 項通過，零失敗／警告。
- `Saved/MultiOutfit-final-format-tests.log`：6 項格式、CRC、引用與跨 outfit 去重測試通過。
- `Saved/NeckHair-shape-test.log`：full accessory tree 隨臉部數值更新、重複套用不累積、回到原值還原通過。
- `Saved/MultiOutfit-final-build.log`：最終 UE build 成功；receipt 的 1,692 個資源項目全部為 NonUFS，包含全 1,691 個資源檔與 catalog。

舊 `KK_346095_all_outfits*.png`（沒有 `skinned`）與 v2 zip 保留作為前一版產物，日常使用請選上述新版。

原始 `C:/portable/KoikatuSunshine/UserData/chara/female/KK_346095.png` 未覆寫，SHA-256 仍為 `BA9B017CBAB3E1DF2760631EAEFA31A583AB246A7B6C136E4E942E46861B5723`。舊單服裝產物也保留。

## 重現

```powershell
& Tools/ok-extract/target/release/ok-preview-pack.exe shared `
  'C:\portable\KoikatuSunshine' `
  'C:\portable\KoikatuSunshine\UserData\chara\female\KK_346095.png' `
  'C:\portable\OpenKoikatsu\Content\IndependentResources\v1' `
  'C:\portable\OpenKoikatsu\Saved\IndependentCards\Release\another_all_outfits.png'
$env:OK_APNG_SOURCE_CARD='C:\portable\OpenKoikatsu\Saved\IndependentCards\Release\another_all_outfits.png'
$env:OK_APNG_OUTPUT='C:\portable\OpenKoikatsu\Saved\IndependentCards\Release\another_all_outfits_apng.png'
# Unreal 遊戲模式執行 ok.ExportApngCard
# ok.VerifyApngExportUI 以 OK_APNG_OUTPUT 驗收全服裝切換及另存
```

輸出檔案不可已存在；轉換器不覆寫來源或既有輸出。
