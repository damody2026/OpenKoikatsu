# KK_346095 新卡載入實測

2026-10-01，本機 AMD Ryzen 9 9950X3D／RTX 5090，Unreal 5.8 Development Editor game mode、D3D12、768×1024。
不提供 KoikatuSunshine GameRoot，使用程式內建資源。

同一程序交錯載入靜態卡與 APNG 卡，各三次；每次使用新的角色 actor，清理本輪產生的角色／攝影機／燈光／背景，GC 後再進入下一輪。
Windows 檔案快取及 shader/DDC 未清空；卡片列表初始化會先讀卡，單獨讀卡測量也會暖化 IO，因此以下是暖快取測量，**不是冷啟動或磁碟冷讀**。
不計 Unreal 啟動、初始卡片列表掃描、單獨解析測量和測試清理時間。

## 中位數

| 階段 | 靜態新卡 | APNG 新卡 |
|---|---:|---:|
| 檔案大小 | 13,988,535 bytes | 17,011,713 bytes |
| Gallery 選取／建立預覽 | 64.4 ms | 107.4 ms |
| 預覽 render thread flush | 0.44 ms | 0.59 ms |
| 獨立角色 Session.Open | 400.3 ms | 395.8 ms |
| LoadSession：模型／貼圖／材質建立 | 4,748.2 ms | 4,746.9 ms |
| 身材、studio 與取景 | 189.1 ms | 200.2 ms |
| 3D render thread flush | 0.37 ms | 0.64 ms |
| 單獨完整 3D 建置 | 5,329.0 ms | 5,363.9 ms |
| 預覽＋完整 3D 建置 | 5,384.5 ms | 5,472.5 ms |
| 預覽＋3D＋shader／四幀等待 | 5,813.3 ms | 5,957.4 ms |

各列是自身的中位數，不能直接相加得到總計中位數。
APNG 預覽選取三次範圍 106.5～109.5 ms；APNG 預覽＋3D 建置為 5.382～5.510 秒；包含 shader／四幀等待為 5.827～6.026 秒。

額外單獨測量：APNG 卡讀取／解析中位數 74.4 ms，APNG 幀拆解 10.2 ms。
這些是在正式 Gallery 計時前分開執行，不能當成 Gallery 內的精確巢狀分段。

## 判讀

APNG 選取比靜態新卡多約 43 ms。對照的靜態預覽是 252×352 一張，新卡是 768×1024 四張；此差異同時包含檔案變大、解析、像素數及四張 texture 建立，並非只量 APNG 格式本身。

完整 3D 的主要成本是 `AKKPreviewActor::LoadSession`，約 4.75 秒，佔 APNG 單獨 3D 建置約 88.5%。
靜態與 APNG 的這一段幾乎相同；目前 APNG 不是完整角色載入的主要瓶頸。
小樣本完整載入差異約 88 ms，仍可能受建立順序／系統負載影響，不應推論成固定的 APNG 額外耗時。

如下一步要加速，應先在 LoadSession 內細分並確認貼圖解碼、mesh／骨架建立、材質建立的成本，再評估共用資源的 Unreal 物件快取或預先轉換。此次僅新增量測工具，未修改產品載入流程或做快取優化。

「shader／四幀等待」是在建置後等待 shader 編譯結束、engine frame counter 至少前進四幀，再 flush render thread；是可重現的準備檢查，**不是 GPU timestamp、首個畫面呈現延遲或精確的使用者可見時間**。
本次測量包含實際 D3D12，沒有使用 NullRHI。

## 重現

```powershell
$env:OK_CARD_DIRECTORY='C:\portable\OpenKoikatsu\Saved\IndependentCards\Release'
$env:OK_PROFILE_STATIC_CARD='C:\portable\OpenKoikatsu\Saved\IndependentCards\Release\KK_346095.png'
$env:OK_APNG_OUTPUT='C:\portable\OpenKoikatsu\Saved\IndependentCards\Release\KK_346095_apng.png'
# Unreal game mode 執行 console：ok.ProfilePortableCards
```

工具：`Source/OpenKoikatsu/OKCardLoadProfile.cpp`，六輪結束後自動退出。
原始數據與 min／median／max：`Saved/APNGCards/load-profile.json`。
測量日誌：`Saved/APNG-load-profile.log`。
編譯日誌：`Saved/APNG-profile-build.log`（Succeeded）。六輪均成功，每輪建立 48 個角色 parts。
