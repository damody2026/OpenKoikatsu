# 內建共用資源與精簡 PNG（KK_346095）

2026-10-02 更新：新轉換器會包含原卡全部 outfit，保留 Coordinate 服裝中繼資料，採 `OKPV0003` 跨服裝去重與索引載入。以下大小及資源盤點是先前 outfit 0 產物的歷史數據；全部服裝的新產物與驗收見 [全服裝 APNG](APNG_ALL_OUTFITS.md)。

四視圖 APNG 已完成：見 [APNG 卡片使用與驗證](APNG_CARDS.md)。新檔 `Saved/IndependentCards/Release/KK_346095_apng.png` 約 17.0 MB，保留相同的獨有資源區塊與共用引用。

本次將 KK_346095 服裝 0 使用的共用模型、骨架、身材／臉部變形資料、材質定義及來源貼圖，改為程式的共用資源。新 PNG 只包含預覽、角色參數、角色調整後的狀態、資源引用，以及角色專用的貼圖／幾何狀態。它不再完整保留原遊戲 KKEx 的所有服裝及貼圖字典。

## 產物與大小

| 產物 | 位置 | 大小 |
|---|---|---:|
| 內建資源 | `Content/IndependentResources/v1/` | 20,304,501 bytes 的資源檔，另加 catalog |
| 新圖卡 | `Saved/IndependentCards/Release/KK_346095.png` | 13,988,535 bytes |
| 身材編輯驗收卡 | `Saved/IndependentCards/Release/KK_346095_edited.png` | 相同共用引用；身高 0.123、胸部 0.789 |
| 舊完整快照卡 | `Saved/IndependentCards/KK_346095_portable.png` | 72,885,466 bytes（歷史產物） |

新卡比舊完整快照減少約 80.8%。原始遊戲卡 30,114,283 bytes，沒有覆寫；SHA-256 仍為 `BA9B017CBAB3E1DF2760631EAEFA31A583AB246A7B6C136E4E942E46861B5723`。新卡與原卡的 PNG 預覽 RGBA 像素一致。

## 共用資源盤點

`catalog.json` 記錄內容雜湊、種類、原始大小、壓縮大小及部件標籤。此包包含本次轉換的 52 個部件所需共用資料，包括 `HeadMod` 頭部及 `BP.profundis` 身體。髮型、服裝與配件的來源網格也採相同規則，不只拆出身體和頭部。

| 類別 | 資源項目數 | 壓縮 bytes |
|---|---:|---:|
| 網格、UV、權重、bind pose、表情變形、配件來源模型 | 420 | 10,091,053 |
| 材質定義 | 69 | 15,970 |
| 共用貼圖、眼睛高光與 ramp | 69 | 9,812,552 |
| 骨架、形狀鍵、碰撞與通用設定 | 21 | 384,926 |

項目數是分拆後的資料塊數，不是完整模型數。共 579 個 catalog 項目，當前卡有 794 個共用資源引用；相同內容以 ID 引用。這個目錄可由後續卡片轉換擴充；目前不是整個遊戲／所有 zipmod 庫的全集。

材質的 shader、貼圖槽與來源定義放在共用包，角色的顏色及數值覆寫留在卡片。主程式既有的 Unreal 材質與 shader 資產仍由 Content 及插件提供。

## 卡片獨有資料

- 26 個獨有資源：角色產生或 MaterialEditor 覆寫的貼圖，以及不等於共用 rest mesh 的角色幾何狀態；同一獨有內容在卡內只存一次。
- 獨有資源原始 bytes 為 14,003,599；角色狀態及參數為 339,558 bytes。
- PNG 預覽及精簡 Custom／Parameter 中繼資料共 127,193 bytes。
- 原本 KKEx 等遊戲區塊不再附加到新卡，避免再次存入原卡 30 MB 的貼圖與其他服裝資料。已轉換的顯示結果由獨立角色狀態保存。
- 共用模型、材質定義、共用圖像與配件模型不可留在 residue；轉換與載入都檢查這個條件。
- 載入需要本程式的內建資源包，不需要來源遊戲或原 zipmod。卡片本身不再包含所有共用資源。

## 執行與安裝

另提供 `Saved/IndependentCards/Release/OpenKoikatsu_BuiltinResources_v1.zip`，解壓至程式／專案根目錄，即會建立 `Content/IndependentResources/v1`。這是安裝用壓縮包，執行時讀取解壓後的資源檔案。

```powershell
$tool='C:\portable\OpenKoikatsu\Tools\ok-extract\target\release\ok-preview-pack.exe'
& $tool shared 'C:\portable\KoikatuSunshine' `
  'C:\portable\KoikatuSunshine\UserData\chara\female\KK_346095.png' `
  'C:\portable\OpenKoikatsu\Content\IndependentResources\v1' `
  'C:\portable\OpenKoikatsu\Saved\IndependentCards\another_builtin_card.png'

# 驗證僅需要卡片與程式共用資源包，沒有 game_root 參數。
$env:OK_BUILTIN_RESOURCES='C:\portable\OpenKoikatsu\Content\IndependentResources\v1'
& $tool verify 'C:\portable\OpenKoikatsu\Saved\IndependentCards\Release\KK_346095.png'
```

轉換器不覆寫既有 PNG。資源採內容 ID 及 LZ4，每個 `.okasset` 載入時驗證 SHA-256。PNG 私有 `okPv` chunk 使用 `OKPV0002` 封包；舊的 `OKPV0001` 完整快照分支仍保留。此格式仍屬於專案內部 schema，尚未定義長期公開交換格式。

Unreal 在空白 GameRoot 載入時設定共用資源路徑至 `ProjectContentDir()/IndependentResources/v1`，也接受明確的 `OK_BUILTIN_RESOURCES` 覆寫。Build.cs 將 catalog 及 `.okasset` 設為 NonUFS runtime dependencies，讓打包時帶上這個目錄；Rust 使用普通檔案讀取，不能只將它們放入不可直接讀取的 pak。catalog 修改會使 UBT 重新盤點 staging 清單。轉出的遊戲資源目錄已加入 gitignore。

## 驗證結果

- Rust 4 項通用封包／checksum 測試通過；2 項本機測試通過，包含高度骨架變化及同一次提取的完整 split/merge 還原。
- split/merge 對比網格、頂點、UV、權重、bind pose、變形、所有貼圖、來源材質、配件模型、眼睛高光／基礎 UV 及身體／頭部骨架。用同一次提取比較，避免兩次 DynamicBone 模擬的浮點差異造成錯誤失敗。
- 檢查卡內獨有資源 ID 不存在於共用包；重複引用、路徑穿越 ID 及資源損毀被拒絕。缺少共用包時明確報錯，不回退到來源遊戲。
- Unreal `IndependentCard + KKCard + KKSideloader` 共 11 項通過，0 失敗、0 警告，包含讀取、身材編輯、PNG 另存及共用引用保留。
- 另存卡直接用 Rust 重開，讀回身高 0.123、胸部 0.789，身材通道的頂點變化檢查通過。
- RHI 實際顯示 48 個可見部件（原始 52 個含不可見部件），基準／身高 1 畫面已檢視，檔案為 `builtin-v2-346095-baseline.png` 與 `builtin-v2-346095-tall.png`。

本次單次暖快取讀取約 0.389 秒。RHI 記錄 session 0.393 秒、Unreal 網格／貼圖／材質建立 5.036 秒、形狀／鏡頭 0.203 秒，共 5.632 秒；不含程式啟動與 shader 等待。這不是受控的 10 卡效能比較。資源拆分已完成，但目前依然使用 transient mesh／PNG 貼圖解碼，尚未改成預先 Cook 的 Unreal 資產或跨角色的 GPU 資源快取。

目前仍只支援服裝 0，沒有來源遊戲的動畫或插件腳本執行；新卡供 OpenKoikatsu 獨立載入，不保證能放回原遊戲還原所有服裝及插件編輯資料。

最終交付目錄為 `Saved/IndependentCards/Release`。在獨立卡片資料夾模式指定此目錄，開啟 `KK_346095.png`；共用資源已安裝於本專案 Content，不必在本機再解壓 ZIP。ZIP 用於其他安裝位置。已核對 UBT receipt 含 580 個 NonUFS 項目（579 資源＋catalog），ZIP 共 17,144,774 bytes，完整 CRC 驗證通過。最終新版 UE 11 項與編輯卡 PortableSession 1 項均通過、0 警告；獨立卡素材需要服裝 0。
