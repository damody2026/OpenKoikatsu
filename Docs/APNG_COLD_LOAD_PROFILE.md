# APNG 冷檔案副本首次載入

2026-10-02，KK_346095 APNG，AMD Ryzen 9 9950X3D／RTX 5090，Unreal 5.8 Development Editor game mode，D3D12，768×1024。

## 單次實測

| 階段 | 時間 |
|---|---:|
| APNG Gallery 預覽 | 118.8 ms |
| Session：內嵌資料及共用資源讀取／重建 | 568.5 ms |
| 模型／貼圖／材質建立 | 5,254.5 ms |
| 身材／studio／取景 | 213.7 ms |
| 預覽＋完整 3D 建立 | **6,157.0 ms** |
| 加上 shader／四幀準備檢查 | 6,613.7 ms |

成功載入 48 parts 與四張 APNG 預覽，不提供遊戲 GameRoot。
主要耗時仍在 LoadSession 的模型／貼圖／材質建立。
先前三次暖快取中位數是預覽 107.4 ms、預覽＋3D 5,472.5 ms；本次首次讀取約多 0.68 秒，但它同時包含新程序／引擎物件初次建立，不能將差值全部歸因於磁碟快取。
兩次測試間另一個 agent 也更新了原生材質載入程式，因此先前暖數字僅作參考，並非同版本的嚴格因果對照。

## 冷檔案條件與限制

此程序未以管理員執行，缺少 Windows 全域檔案／standby 快取清除權限。因此使用隔離的冷檔案副本方法，**沒有宣稱做過全系統快取清除、重新開機或冷 shader 編譯**。

1. 建立新的 UUID 資料夾，複製 APNG 卡、catalog 與全部 579 個內建資源。共 581 個檔案、37,457,242 bytes。
2. 目的檔用 Win32 `FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH` 寫入，再以不經快取的讀取驗證 SHA-256。來源檔可暖化，測試只讀新的目的檔；目的檔沒有由此工具進行 buffered 預讀。
3. 開啟新的 Unreal 程序，`OK_BUILTIN_RESOURCES` 指向新副本。Gallery 初始列表指向空資料夾，停用量測工具原有的單獨讀卡／拆幀預熱。
4. 只執行一次 APNG 預覽與 3D 載入。

[Microsoft CreateFile 說明](https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-createfilea) 定義上述兩個旗標的 Windows system cache 寫入行為。
檔案 metadata、硬體／SSD 控制器快取及 shader/DDC 保留；其他程序（例如檔案掃描）仍可能觸碰副本，未透過 ETW 或實體磁碟追蹤驗證每次讀取是否為 cache miss。
這是有明確準備流程的冷檔案近似測試，而非經證明的全系統冷快取數字。

數字排除 Unreal 啟動時間、測試副本建立／驗證與清理。
shader／四幀準備欄位沿用暖測的準備判定，不是精確 GPU timestamp 或首個畫面呈現延遲。

## 重現與記錄

先執行 `python Tools/prepare_cold_card_files.py`，以 `Saved/APNGCards/cold-files-manifest-path.txt` 取得新副本 manifest。
將 `OK_CARD_DIRECTORY` 指向 manifest 的 `empty_gallery`、`OK_BUILTIN_RESOURCES` 指向 `builtin`、`OK_APNG_OUTPUT` 與 `OK_PROFILE_STATIC_CARD` 均指向新 `card`，設定 `OK_PROFILE_COLD_FILES=1`。
在新 Unreal game mode 程序執行 `ok.ProfilePortableCards`，單次測量完成後自動退出。
每次新的冷檔案測試都應重新產生副本，不重用已載入過的 UUID 資料夾。

- `Saved/APNGCards/load-profile-cold-files.json`：原始數據與測試條件。
- `Saved/APNG-cold-load-profile.log`：單次載入 PASS。
- `Saved/APNG-cold-profile-build.log`：編譯 Succeeded。
- `Saved/APNGCards/ColdFiles/d51245b4-9bfa-4c76-a847-9fced7e7fb96/manifest.json`：完整副本清單與雜湊。

原卡、內建資源來源與產品載入流程沒有因本次測量而改寫。
