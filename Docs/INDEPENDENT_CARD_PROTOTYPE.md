> 更新：共用資源拆分及 KK_346095 精簡卡已完成，現行結果見 [BUILTIN_RESOURCES.md](BUILTIN_RESOURCES.md)。下列完整 snapshot 是先前原型紀錄。

# 獨立圖卡第一個可執行原型

本階段使用指定的 KK_346095 卡從既有遊戲資源轉換一次，得到含模型、骨架、貼圖、材質參數、身材／臉部調整鍵、手型與碰撞器設定的 snapshot。載入 snapshot 時沒有 game_root，不執行原遊戲 bundle、全域 mods 或動畫掃描。它以現有 Unreal transient skeletal mesh 路徑顯示角色，仍會在 UE 端解碼貼圖、建立網格與材質。

## 產物

- `Saved/IndependentCards/KK_346095_portable.png`：實驗性单檔圖卡，原本圖片與 KKS 卡片資料保留，資源放在 PNG 的私有 ancillary chunk `okPv`。
- `Saved/IndependentCards/KK_346095.okpreview`：相同資源的獨立檔案，舊 PNG 可搭配同名副檔名使用。
- `Saved/IndependentCards/png-verification.json`：空白遊戲路徑的載入與身材驗證。
- 遊戲衍生產物放在 Saved，本機測試使用，不加入原始碼。

原型封包 schema 1 使用 `OKPV0001` 標頭、schema 數值、未壓縮長度、CRC32 與 LZ4 payload。payload 是本專案内部序列化型別；日後相關型別改動須提升 schema。它目前不承諾跨引擎、跨程式版本或跨平台相容，**不是最終公開 PNG v1**。

單檔 PNG 重開時由 loader 讀取 Custom 中的最新身材／臉部數值並套用，不需要 Gallery 額外補回。原卡的 KKEx 插件資料原樣保留；獨立模式不執行來源插件的數值覆寫（例如 PushUp），因此身材以編輯器保存的 Custom 數值為準。圖片轉存服務可能移除私有 chunk，分享時應傳送原始檔。

## 在程式中操作

1. 啟動 OpenKoikatsu，將左側「卡片資料夾」填為 `C:\portable\OpenKoikatsu\Saved\IndependentCards`，按讀取資料夾。
2. 選 `KK_346095_portable.png`，按載入 3D。使用服裝 0；目前沒有其他服裝或來源動畫。
3. 調整身材滑桿。單檔 PNG 的預覽照片仍是原圖片；3D 預覽使用編輯後數值。
4. 回卡片頁另存到指定輸出資料夾。檔名為 `_edited` 或增加數字，避免覆寫原卡；單檔卡保留內嵌資源。舊卡搭配 `.okpreview` 的情況會複製同名資源檔。
5. 沒有 3D 原型包的舊卡，也可開啟「編輯身材數值」並另存 PNG；此時只有卡片資料編輯。

資料夾／輸出位置保存於 `UKKCardWorkspace` 的 Game config。測試可以設環境變數 `OK_CARD_DIRECTORY` 暫時覆蓋卡片資料夾；有設定此變數時它優先於 UI 設定。

## 同目錄 zipmod

獨立資料夾模式只讀取卡片父目錄的 `*.zipmod`，不遞迴掃子目錄。讀 ZIP central directory 與 manifest，依 GUID 匹配卡片引用，檔名不必對應。不同內容宣告同一 GUID 會列為衝突，缺少或格式錯誤會顯示診斷；內容相同的重複檔不算衝突。支援 stored／deflate manifest，核對 CRC 與長度，尚不支援 ZIP64。

另存時可勾選複製已找到的 zipmod；目的地同名但內容不同時會停止，不覆寫。**找到 zipmod 目前表示依賴發現成功，不表示已支援其中所有模型、shader 或腳本的獨立轉換。** 單檔原型已包含這張卡轉出的部件，顯示它時不用再讀 zipmod。

## 重現轉換與驗證

從專案根目錄執行 PowerShell。工具拒絕覆寫既有輸出，重跑請換新檔名。

```powershell
cargo build --release --manifest-path Tools/ok-extract/Cargo.toml --bin ok-preview-pack
$tool = '.\Tools\ok-extract\target\release\ok-preview-pack.exe'
& $tool export 'C:\portable\KoikatuSunshine' 'C:\portable\KoikatuSunshine\UserData\chara\female\KK_346095.png' 'C:\portable\OpenKoikatsu\Saved\IndependentCards\test.okpreview'
& $tool embed 'C:\portable\KoikatuSunshine\UserData\chara\female\KK_346095.png' 'C:\portable\OpenKoikatsu\Saved\IndependentCards\test.okpreview' 'C:\portable\OpenKoikatsu\Saved\IndependentCards\test_portable.png'
& $tool verify 'C:\portable\OpenKoikatsu\Saved\IndependentCards\test_portable.png'
```

`verify` 只接收資源檔或 PNG 的路徑，不接受遊戲根目錄。測身體 channel 0、4、22 的 0／0.5／1，檢查所有頂點有限且幾何實際改變。PNG preview 解碼前移除大型 snapshot chunk，原卡儲存則保留它。

```powershell
cargo test --release --manifest-path Tools/ok-extract/Cargo.toml --lib portable::tests
$env:OK_TEST_PREVIEW_PACK='C:\portable\OpenKoikatsu\Saved\IndependentCards\KK_346095_portable.png'
& 'D:\UE5.8\Engine\Build\BatchFiles\Build.bat' OpenKoikatsuEditor Win64 Development '-project=C:\portable\OpenKoikatsu\OpenKoikatsu.uproject' -waitmutex
& 'D:\UE5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' '.\OpenKoikatsu.uproject' -unattended -NullRHI -nosound '-ExecCmds=Automation RunTests OpenKoikatsu.IndependentCard' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=C:\portable\OpenKoikatsu\Saved\IndependentCards\AutomationReport' -stdout -FullStdOutLogOutput
$env:OK_CARD_DIRECTORY='C:\portable\OpenKoikatsu\Saved\IndependentCards'
& 'D:\UE5.8\Engine\Binaries\Win64\UnrealEditor.exe' '.\OpenKoikatsu.uproject' /Game/test -game -unattended -nosound '-ExecCmds=ok.VerifyIndependentPreview' -windowed -ResX=1280 -ResY=900 -stdout -FullStdOutLogOutput
```

Unreal automation 覆蓋三種 Custom layout 的身材與臉部 roundtrip、未知區塊與 header metadata 保留、磁碟寫入失敗時記憶體資料不變、同目錄 GUID／衝突／CRC，以及空白遊戲路徑 FFI session 和真正單檔 PNG 另存。圖像驗證命令產生 `portable_baseline.png` 與 `portable_tall.png`，等待 shader compilation 後擷取。

## 下一階段

- 補完整十張不同卡的冷／暖載入 profiling；本階段的单卡驗證不是十卡 benchmark。
- 分離共用模型／材質與每卡參數，建立 UE cook 的資源包，減少 PNG 重複貼圖和 UE 每次建模。
- 將內部 snapshot 換成穩定 CharacterSpec／資源 ID／依賴 manifest，定義公開 PNG v1 與遷移規則。
- 支援卡片同目錄 zipmod 的部件轉換，明確拒絕不支援的 shader／腳本，加入內容雜湊與快取失效規則。
- 提供獨立製作的基礎資源後，才驗收「轉換前也不需安裝遊戲」。

## 先前 KK_398530 測試紀錄（2026-10-01）

指定卡一次轉換產生 63 個原始部件，原型包 40,289,912 bytes，內嵌 PNG 54,907,784 bytes。單檔 PNG 與原卡的 252×352 預覽像素雜湊一致。純 Rust 驗證空白 game_root、44 身體與 52 臉部通道，測試身體 0／4／22 的幾何變形。

Unreal 實際 RHI 圖像驗證建立 59 個可見部件（其他部件原本隱藏），讀取单檔 PNG/session 0.475 秒，網格／貼圖／材質建立 4.576 秒，身材與鏡頭設定 0.418 秒，合計 5.470 秒。這是同一台機器的一次暖載入紀錄，未計程式啟動與 shader 等待時間，不能當成十卡冷載入統計。仍以既有材質管線呈現，外觀精確度需再與原遊戲比對。

先前圖像另存為 `portable-398530-baseline.png`／`portable-398530-tall.png`。Rust 封包標頭／長度／CRC／PNG 完整性測試 3 項通過；Unreal 的身材 roundtrip、同目錄 zipmod、FFI portable session、內嵌 PNG 另存測試 4 項與既有 KKCard 回歸 6 項通過。最新 build 與 automation 輸出放於同一 Saved/IndependentCards 目錄。

## 現行 KK_346095 測試紀錄

來源為 `UserData/chara/female/KK_346095.png` 的頂層檔案，沒有使用 perfect 或 old_new 中的同名版本。一次轉換約 73.99 秒，產生 52 個原始部件，snapshot 42,771,171 bytes，內嵌 PNG 72,885,466 bytes。原卡與複製來源 SHA-256 均為 `BA9B017CBAB3E1DF2760631EAEFA31A583AB246A7B6C136E4E942E46861B5723`，PNG 預覽像素也一致。

空白 game_root 的純 Rust 載入約 0.651 秒。另存 PNG 後獨立重開，身高 0.123、胸部 0.789 均讀回一致；測試報告為 `edited-verification-346095.json`。Unreal 實際 RHI 建立 48 個可見部件，session 0.653 秒、網格／貼圖／材質 5.504 秒、身材／鏡頭 0.237 秒，合計 6.394 秒，未計啟動與 shader 等待。當前畫面為 `portable_baseline.png`／`portable_tall.png`。

這張卡的身高包含骨骼縮放變化，驗收需同時比較局部位置與縮放，不能只比較位置。十張不同卡、正式 cooked 資源包與公開 PNG v1 仍屬後續里程碑。

最終驗收（16:35）：UE build 成功，`AutomationReport-346095/index.json` 為 10 項成功、0 失敗、0 警告，包含 4 項 IndependentCard 與 6 項既有 KKCard 回歸。PortableSession 直接載入另存的單檔 PNG，核對身高 0.123／胸部 0.789；無 GameRoot fallback。另有 Rust 三項通用封包測試及指定卡的骨架匯出縮放測試通過。資料夾偏好写入本機 Game config，未使用 defaultconfig 保存到專案預設檔。
