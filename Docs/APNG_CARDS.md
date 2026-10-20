# APNG 四視圖卡片

2026-10-01：已實作 KK_346095 的四視圖 APNG 新卡、Gallery 播放與 3D 匯出按鈕。

2026-10-10：轉存 APNG 時另輸出同名 `.lowpoly.glb` 低模（Instant Meshes 重網格，保留共用骨架與蒙皮權重），見 [APNG 低模伴隨檔](APNG_LOWPOLY.md)。

2026-10-02：[全服裝 APNG](APNG_ALL_OUTFITS.md) 已包含 KK_346095 原卡九套 outfit，約 25.2 MB；`KK_346095_all_outfits_skinned_apng.png` 保留頭部權重，修正改身材時臉髮與衣領錯位。以下原始產物數據保留供比較。

## 使用

1. 在卡片列表讀取 `Saved/IndependentCards/Release`，選取已轉換為內嵌資源的新卡。
2. 填寫「另存資料夾」，載入 3D。可先調整身體／臉部數值與目前姿勢。
3. 按「另存 APNG 新卡（四視圖）」。列表與 3D 編輯面板都有這個按鈕。
4. 擷取期間介面暫停操作，完成後恢復；輸出為新的 `.png`，不覆寫來源。

普通「另存新卡（身材／名稱）」仍保留原預覽。更新身材後，使用 APNG 按鈕重新擷取四視圖。
外部 `.okpreview` 原型包需先轉換為內嵌資源卡，才能使用 APNG 匯出。

## 格式

遵循 [W3C PNG/APNG 規格](https://www.w3.org/TR/png-3/)。副檔名維持 `.png`。

| 次序 | 視圖 | 尺寸 | 時間 |
|---|---|---|---|
| 0 | 臉部特寫 | 768 × 1024 | 2 秒 |
| 1 | 正面 | 768 × 1024 | 1 秒 |
| 2 | 側面 | 768 × 1024 | 1 秒 |
| 3 | 背面 | 768 × 1024 | 1 秒 |

`acTL.num_frames=4`，`num_plays=0`（無限循環）。每幀 `fcTL` 使用完整畫布、零偏移、SOURCE blend、NONE dispose；首幀使用標準 IDAT，後三幀使用 fdAT。首幀是靜態 PNG 檢視器看到的臉部特寫。
不支援 APNG 的檢視器通常只顯示首幀；OpenKoikatsu 自行解碼並播放此四視圖格式。

PNG 的 `okPv` 區塊保留原樣，包含全部 outfit 的共用資源引用與角色獨有資源；IEND 後保留可編輯的 Custom／Parameter 及完整 Coordinate 資料。四張動畫預覽顯示匯出時選取的服裝；其他服裝的完整 3D 資料仍在同一張卡中，可於 Gallery 切換。
APNG 只增加預覽圖片，不將共用模型／材質重新塞回卡片。應用程式仍需安裝 `Content/IndependentResources/v1` 的內建資源包。

## 實作

- `KKAnimatedPng`：APNG 打包、序列／CRC／完整幀檢查、拆出獨立 PNG、時間到幀的對應。
- `OKCardPreviewCapture`：768 × 1024 固定 render target，四個正交鏡頭。依實際可見的已蒙皮頂點計算身體／臉部取景，不受視窗大小影響。三視圖使用同一縮放比例。
- 擷取會暫停角色 tick，等待 shader 完成與不同渲染幀，完成／錯誤時還原角色 tick。線性 render target 只做一次 sRGB 編碼，避免預覽偏亮。
- Gallery：只拆解圖片幀；不將 13.8 MB 的 `okPv` 當圖片解壓。選取時建立四張 texture，播放只切換 UImage brush，不逐幀解壓資源或重新建立 texture。
- 四張 RGBA 預覽 texture 的像素記憶體共 12 MiB；選取另一張卡時釋放舊引用。
- 舊靜態 PNG 可以繼續預覽與存檔。動畫解碼失敗時退回靜態預覽。

2026-10-02 起，`ok-preview-pack shared` 會從原卡轉換全部 outfit，以 `OKPV0003` 索引封裝保存。相同獨有資料跨服裝只存一次，載入時僅還原選取的服裝；共用模型及材質仍放在內建資源包。舊 `OKPV0001/2` 單服裝卡仍可載入 outfit 0，需由原卡重新轉換才能補齊其他服裝。APNG 沿用既有 Unreal 材質，顯示結果可能與來源遊戲有差異。

## 已產生的檔案

- `Saved/IndependentCards/Release/KK_346095_apng.png`：17,011,713 bytes，約 17.0 MB（16.22 MiB）。
- 靜態新卡：13,988,535 bytes；四視圖預覽增加 3,023,178 bytes。
- `okPv` 的 13,861,330 bytes 完全不變，原卡 SHA-256 未變更。
- `Saved/APNGCards/view0.png` 至 `view3.png`：Unreal 原始四視圖擷取。
- `Saved/APNGCards/verification.json`：全部 PNG chunk CRC、Pillow 四幀／秒數／loop、逐像素與原始擷取一致、獨有資源區塊不變的檢查。
- `Saved/APNGCards/gallery.png`：實際 Unreal Gallery 顯示 APNG 的截圖。

## 驗證與重現

UE build 成功。`Saved/APNGCardReport/index.json`：13 項 APNG／IndependentCard／KKCard／KKSideloader 測試成功，零失敗、零警告。
涵蓋四幀像素解碼、秒數與循環邊界、有限循環、CRC 破壞／截斷／缺幀拒絕、身材與名稱另存及完整動畫／獨有資源區塊保留。
`Saved/APNGEditedReport/index.json`：改為身高 0.123、胸部 0.789 的 APNG，在空白 GameRoot 下重新開啟成功。
`Saved/APNG-gallery.log`：實際 engine tick 觀察 UImage 播放四幀並完成 5 秒循環。
`Saved/APNG-export-ui.log`：實際呼叫 Gallery 匯出按鈕，完成載入 3D、改身高 0.222／胸部 0.444／名稱、四視圖擷取、另存與空白 GameRoot 重新載入；操作鎖定及完成後還原也通過。

自動匯出（Unreal 遊戲模式 console）：

```powershell
$env:OK_APNG_SOURCE_CARD='C:\portable\OpenKoikatsu\Saved\IndependentCards\Release\KK_346095.png'
$env:OK_APNG_OUTPUT='C:\portable\OpenKoikatsu\Saved\IndependentCards\Release\KK_346095_apng.png'
# 在遊戲模式執行 ok.ExportApngCard，完成時寫入檔案並退出。
```

`ok.VerifyApngGallery` 以 `OK_APNG_OUTPUT` 測試真實 Gallery texture 切換、時間邊界、engine tick 和循環。
`ok.VerifyApngExportUI` 測試載入 3D、改身材／名字、逐一切換並擷取全部 outfit、以最後一套服裝按 Gallery 匯出，再逐一以空白 GameRoot 開啟輸出卡的全部 outfit；測試輸出放在 `Saved/APNGCards/UiExport/<UUID>`，各套四視圖放在 `Saved/APNGCards/AllOutfitViews`。
這些 console 驗證命令會自動退出，用於開發驗收。
