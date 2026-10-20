# 直接啟動讀卡模式

雙擊專案根目錄的 `CardReaderDevelopment.cmd`，即可啟動可操作的讀卡遊戲視窗，不必先開 Unreal Editor 或進入 CrowdRoom。預設1600×1000；選卡後按「加载3D」進入人物預覽。人物載入後可用A＋左鍵噴水槍、R清除。

使用 `Tools/ok_engine.ps1`／`ok.engine_root()` 選出的引擎（`C:\portable\OpenKoikatsu` 為 `D:/PhysX/UE5.8`，見 [DUAL_LINE_DEVELOPMENT.md](DUAL_LINE_DEVELOPMENT.md)）的 `UnrealEditor.exe -game`，明確指定Template_Default及OpenKoikatsuGameMode。這是開發版啟動器，仍需要目前引擎與已編譯專案模組；不是獨立封裝的exe。編輯器模組與引擎不符時會先自動重編（`-NoBuild` 跳過），不修改Crowd啟動器或永久設定。

預設使用目前專案設定的KKS根目錄與卡片資料夾。啟動子程序前清除本輪自動載3D／指定卡的環境控制，開啟正常卡片清單；環境只作用於此啟動流程。

## 可選參數

```bat
CardReaderDevelopment.cmd -CardDirectory "C:\My Cards"
CardReaderDevelopment.cmd -StreetHeroines
CardReaderDevelopment.cmd -StreetHeroines -- -CharacterMCP "-CharacterMCPCard=C:/path/card.png"
CardReaderDevelopment.cmd -WindowWidth 1920 -WindowHeight 1080
CardReaderDevelopment.cmd -EngineRoot "E:\PhysX\UE5.8"
CardReaderDevelopment.cmd -DryRun
```

`-CardDirectory`指定 APNG／獨立內嵌資源卡片資料夾，僅作啟動時初始來源；之後可在介面改成 KKS 遊戲目錄。沒有內嵌或同名 okpreview 資源的原生 KKS 卡，請用 KKS 遊戲目錄模式。`-DryRun`僅檢查入口檔案與列出參數，不開遊戲。`--` 之後的參數原樣轉給 UnrealEditor（例如 `-CharacterMCP`、`-CharacterMCPCard=`、`-CharacterMCPDir=`，以人物編輯器 MCP 自動載卡與截圖）。日誌與啟動紀錄（PID／DLL／參數）位於 `Saved/Logs/CardReader`；缺少引擎、DLL或卡片目錄時會顯示錯誤，cmd保留視窗以便閱讀。

最後覆核：讀卡遊戲視窗持續 Responding=True，日誌確認載入0939與OpenKoikatsuGameMode，未出現Fatal error或Unhandled Exception。啟動器與文件相符；本輪沒有新增修正。

## APNG／KKS 來源切換

重啟 `CardReaderDevelopment.cmd`，在讀卡清單頂部選「APNG 內嵌資源卡片」或「KKS 遊戲目錄卡片」。APNG 目錄可直接填 `C:/portable/OpenKoikatsu/Saved/APNGCards`，按「載入此 APNG 目錄」；選卡再按「加载3D」。回到讀卡清單即可切换來源，APNG 目錄會被記住。

第三個來源「街頭女主角卡片」直接開 `Content/CharacterCards/StreetHeroines`（街頭模式女主角的 okLb 圖庫卡），不覆寫記住的 APNG 目錄；切回「APNG 內嵌資源卡片」會回到記住的目錄（沒有就用 `CrowdAPNG`）。`-StreetHeroines` 讓讀卡器一開就在這個來源（與 `-CardDirectory` 互斥）；背景預熱也會把這個資料夾加進 `--also`，中途切過去開卡同樣走快取。

同一張未修改的 APNG／portable 卡、同一服裝重開時重用模型；更新卡檔或外部原型包、換服裝會重新載入。第一次仍需建立 Unreal 資源，不能一概宣稱所有 APNG 都比 KKS 快。A＋左鍵噴射、R 清除；水槍性能與實測限制見 `Docs/PAINT_PERFORMANCE284.md`。

## 新版 OK 卡的 APNG 預覽

存成 OK 卡（原卡沒有舊式 okPv 資源）或「另存 APNG 新卡」時，卡片預覽圖是 APNG（768×1024，無限循環）：

1. 原卡圖 3 秒：原卡片影像等比縮放置中，四周補與視圖相同的灰底（0.18 線性灰，sRGB 118）。卡片已是舊的 OK 卡 APNG 時，取它的第一幀。
2. T-pose 正面、側面、背面各 1 秒：擷取前角色切到 T-pose，等骨架更新後才量外框（T-pose 手臂較寬），拍完恢復原本的動作／Studio 動畫／走路。

2026-10-17 以 `ok.ExportApngCard` 實測 C030：acTL 4 幀、無限循環，fcTL 延遲 3/1、1/1、1/1、1/1 秒，768×1024 RGBA；第一幀為原卡圖置中，三視圖為 T-pose。一般存檔需要先載入這張卡、這套服裝的 3D 才會拍；沒載入 3D 時保留原卡圖。第一幀仍是一般 PNG，只讀靜態圖的程式（包括原遊戲）看到的是原卡圖。APNG 區塊在 Rust 依基礎函式庫重做 OK 卡時原樣保留。批次匯出用 `ok.ExportApngCard`（`OK_APNG_SOURCE_CARD`、`OK_APNG_OUTPUT`）。程式：`FKKAnimatedPng::BuildOkCardPreview`／`FitCardImage`、`OKCardPreviewCapture::BeginTPoseViews`；自動化測試 `OpenKoikatsu.APNG.OkCardProfile`。

大小與速度（同日第二輪）：
- 各幀以 zlib 等級 9 壓縮（`GetCompressed(-9)`），三視圖的 sRGB 轉換與壓縮移到工作執行緒平行做，遊戲執行緒只負責拍攝。C030 四幀 548/792/759/792 KB → 494/621/584/623 KB，合計 2.89 → 2.32 MB（−20%）。
- 原卡圖以 Catmull-Rom 雙三次放大（含 alpha 合成），比雙線性銳利。
- 讀卡器選卡時只同步解第一幀，其餘幀在工作執行緒解碼，回到遊戲執行緒時以世代號確認仍是同一張卡才建貼圖（`AnimatedPreviewGeneration`）；測試用 `DecodeRemainingPreviewFramesNow` 同步補完。
- 未採用：改存 RGB（再省約 7%，需自寫編碼器）、只存差異區域的 delta 幀（讀取端要合成）。

## 載入速度與開卡快取（OK 卡）

目標：OK 卡載入 3 秒內、原作 KKS 卡 5 秒內，換服裝同樣。做法是「點到之前就已經建好」，再加上原生端每次開卡的固定成本壓低：

- **開卡快取**：每張卡 × 每套服裝的原生結果存在 `%LOCALAPPDATA%/OpenKoikatsu/oklib-cache`（上限 `OK_LIBRARY_CACHE_GB`，預設 64）。命中時原生 0.1–0.4 秒，讀卡器整體約 1.6 秒（UE 建網格／材質約 1.4 秒）。
- **背景預熱（已停用，2026-10-19）**：`Tools/run_card_reader_development.lua` 不再開 `ok-card-warm` 行程，也不設 `OK_WARM_PRIORITY`；讀卡器開卡時自己建，結果照樣存進開卡快取。`ok-card-warm` 仍可手動執行（用法見其檔頭）。以下兩條只在手動設定 `OK_WARM_PRIORITY` 並自行啟動預熱行程時才有作用。
- **優先請求**：讀卡器在清單選卡、按載入時，把卡片路徑追加到 `OK_WARM_PRIORITY`。預熱行程每建完一筆就重讀，先以一般優先權建最新請求卡片的所有服裝。
- **不重複建置**：讀卡器開一筆未快取的卡時也會認領；若預熱行程正在建同一筆，就等它完成（最多 30 秒）再讀快取。本行程自己的認領不會等待。
- **行程內預熱**：讀卡器開啟時背景呼叫 `ok_prewarm`（`FOkSession::Prewarm`），先建好清單、mods 目錄、GUID 索引、StripAll、UncensorSelector／MaterialEditor manifest、H 配對表等記憶化，第一張卡不必再付這些成本。
- **mods 樹掃描只做一次**：每個 zipmod 的 manifest、GUID、覆寫清單依「封存檔世代」記憶（函式庫內的檔案以邏輯鍵，其它以路徑＋大小＋修改時間，`gamefs::zip_memo`）；卡包只在它於 `mods/` 底下提供內容時才改變這些記憶的鍵（`mounted_pack_mods_identity`）。
- **純影像轉換快取**：法線轉換、遮罩、疊圖、翻轉等以來源內容雜湊記住結果（`png_transform_cache`，1 GiB），換服裝與共用原版貼圖的卡直接重用。

量測：`OK_PROFILE=1` 印出各階段時間與 `SPOT`（熱點累計時間／次數）；`ok-card-open-bench --sequence LIB CARD[@OUTFIT] ...` 在同一行程依序開卡（不讀寫快取），可量換卡／換裝的真實成本。遊戲內以 `OK_LOAD_PROFILE=1` 加 `OK_AUTO_SEQUENCE="~8,C066,@1,C068"`（`~N` 閒置 N 秒、`@N` 換服裝、其他為卡名）自動操作，日誌有 `OKGUI_SEQ done ms=` 與 `OKGUI_LOAD total_ms=`。

- **背景變體不擋前景**：讀卡器在載入後預建的穿脫狀態變體（半脫等），在行程內以背景、可被前景開卡搶先的方式建（使用者下一張卡優先）。有 `OK_WARM_PRIORITY` 時則改以 `卡片<TAB>服裝<TAB>S0,..,S8` 寫進優先清單交給預熱行程，再等快取出現（`ok_session_cache_ready`）。
- **UE 端**：貼圖快取存未壓縮 mip（上限 RAM/8，1–8 GB），遊戲執行緒建貼圖不再 LZ4 解壓；貼圖解碼最多 16 條執行緒；逐幀建構每幀 25 ms；共用配對表與動作目錄在啟動時預熱。`OK_LOAD_PROFILE=1` 另印 `OK progressive part ... ms=` 與 `OKCARD_PROFILE progressive ...`（貼圖／材質／骨架網格）。

未快取時的原生開卡（2026-10-17 第二輪）：
- **mods 視圖切換便宜化**：卡包會改變 zip 清單（每張新卡一個視圖）。清單目錄改為合併每個 zip 已記住的條目（只複製勝出者，0.5→0.13 秒）；MaterialEditor 區段、StripAll、無修正來源以 zip 為單位記住（函式庫記錄的 zip 內容不變，以路徑記）。
- **函式庫回退條目惰性讀取**：函式庫記錄的 zip 條目若內容需從真實 zip 讀，`by_index` 不再立刻解壓，等真正讀取才解（只看檔名的掃描原本會把整個 zip 解一遍）。
- **原作卡**：安裝目錄的 GUID 索引一次建好（`tree_guid_index`），真實 mods 樹的走訪結果在目錄監看未報變更前重用（`watched_tree_zips`）；`card_zipmods` 1.2→0.03 秒。讀卡器在兩種來源模式都預熱基礎函式庫（原作卡也是從函式庫開），並預建安裝目錄的 GUID 索引。
- **頭部基礎貼圖、無修正 bundle 與網格、眼部翻轉**在行程內記住。
- **原生開卡時讀卡器限 15 fps**（`OKThrottleForNativeOpen`），進入 UE 建構時恢復：不限幀時渲染搶走開卡需要的 CPU（同條件 8–12 秒→5–7 秒）。
- 手動執行 `ok-card-warm --also <目錄>` 可一併預建 KKS 安裝目錄的 `UserData/chara/female`、`male`，原作卡與 OK 卡走同一個開卡路徑（`open_card_like_reader`）。

第三輪（物件平行抽取）：
- **物件預取**：清單階段一結束，就依卡片資料預測頭髮、配件、衣物、上衣附屬部件要抽的 (zip, entry, 搖動)，在背景執行緒以相同的穿脫狀態與開關先跑 `meshes_for_entry_shake`；主執行緒同時做基礎 bundle、身體、臉、無修正，物件迴圈到時直接取結果（還沒好就等）。鍵與原本的記憶鍵完全相同，預測錯只是白做；萃取結束前一定等背景工作做完（`PrefetchGuard`），不會晚於卡包卸載。`OK_NO_ENTRY_PREFETCH=1` 可關閉對照。
- **像素運算**：身體上色改為按列平行、顏色轉換移出迴圈；BC1/BC3/BC7 大貼圖按區塊列分段平行解碼。結果逐位元相同（`ok-card-open-bench --compare LIB CARD...` 比對新開結果與快取條目）。
- **背景開卡讓路**：讀卡器內的穿脫變體預建標為背景（`ok_set_thread_background`），前景開卡進行中時它在階段邊界放棄並釋放卡包掛載。
- **卡片體型在工作執行緒套用**：原生 session 的體型值與 UE 讀到的卡片值不同時，原本在遊戲執行緒 `ApplyShapes`（0.3–0.6 秒），改在工作執行緒開卡後就 `ApplyShape`。

第四輪：
- 預取也涵蓋原版頭髮欄位、沒有 KKEx resolve 的配件欄位；命中時直接交出結果、不再複製一份到執行緒記憶（網格含貼圖位元組，一份數十 MB）。
- 函式庫記錄的 zip 內容不變：zip 清單簽章（掛著同一卡包時）、各 zip 的清單條目與 GUID 都以路徑記住，新視圖只處理新增的 zip；toon ramp 以 zip 為單位記住。
- 量測工具（同機在跑完整編譯）：未快取原生 2.96–3.8 秒（C087 2.96、C093 3.00、C030 3.24）。

遊戲內冷快取實測（第三輪，負載 63%）：C058 4.6 秒、C093 4.6、C087 5.1、C044 5.5、C020 6.1、重卡 C071 7.4、行程第一張 8.0；量測工具中原生最小值 2.9–3.9 秒（C071 5.1）。

實測（2026-10-17，RTX 5090／32 執行緒，同機另有遊戲在跑）：已快取的 OK 卡換卡 2.1–3.7 秒、換裝 1.8–2.0 秒；55 個部件的重卡首次 6–7.6 秒、再次 3.2 秒。未快取的卡仍是單執行緒原生轉換：熱機行程約 2–6 秒、行程第一次約 10–12 秒，原作卡 8–11 秒。

## 載入速度實驗開關（2026-10-17 第五輪，預設全部關閉）

> 2026-10-18 結論：目標改為「新版 OK 卡第一次被載入、沒有任何快取」的速度。以這個目標看，本節的方法都不算有效：嵌入成品（方法 1、8）與 memo（方法 3–5）本質上是把快取放進卡片；原始貼圖、預算、鄰卡預建對第一張沒有作用；材質共用在一輪實測中第一張少約 0.9 s，但在雜訊範圍內。第六輪（下一節）的預熱與重複工作移除，對第一張約 −0.4 s。後續工作改看下方「沒有快取的第一次載入」。

九個方法都做成環境變數開關，預設行為不變。旗標全關時，原生開卡結果與修改前的建置逐位元相同（`ok-card-open-bench --compare`，含帶卡包的 C031）；memo 重播的結果也逐位元相同。

| 開關 | 方法 | 做法 |
|---|---|---|
| `OK_RAW_TEXTURES=1` | 2 去 PNG 中介 | 管線內的貼圖改用 `OKRAW\0\2\0`＋寬高＋LZ4 RGBA（`teximg.rs`）；所有讀取端（Rust、`ok_raw_texture.h`、KKTextureRuntime／KKRuntimeMaterial）兩種都收。寫成檔案的輸出仍是 PNG。開卡快取鍵含此旗標。 |
| `OK_CARD_BAKED=1` | 1、8 卡內嵌開卡成品 | `ok-card-bake LIB OUT [--outfits first\|all] [--codec lz4\|zstd:N] CARD...` 把開卡快取條目寫進卡片的 `okBk` chunk（`cardbake.rs`）。條目以內容切塊（`chunk_spans`）共用，同卡各服裝的身體、臉、頭髮只存一份。快取鍵計算時去掉 `okBk`／`okMm`，烘焙前後的卡共用同一個鍵。 |
| `OK_CARD_MEMO=tex,res,mesh` | 3、4、5 卡內嵌中間結果 | `ok-card-bake --memo KINDS [--no-open]` 把開卡時用到的記憶結果寫進 `okMm`（`memosnap.rs`）。tex＝貼圖轉換與 texel 解碼，res＝mods 目錄視圖與 GUID 索引，mesh＝物件網格。開卡時重播，只做剩下的組裝。 |
| `OK_MATERIAL_DEDUPE=1` | 6 材質共用 | 編輯器下每個部件都會新建 UMaterial 並 PostEditChange。改為以「圖形簽章」（運算式屬性，本身物件以序號、執行期貼圖以來源內容雜湊＋取樣設定代表）查已完成的相同材質，命中就直接用（`KKMaterialDedupe.cpp`，最多 768 個）。 |
| `OK_PART_BUDGET_MS`、`OK_PART_STEPS` | 7 部件建構節奏 | 原為每幀 25 ms、最多 8 個部件。量測顯示骨架網格建構每張卡只有 11–17 ms，移到背景沒有意義；真正的成本是分散在多幀，所以改為可調整每幀預算。 |
| `OK_PRELOAD_NEIGHBOR=1` | 9 鄰卡預建 | 讀卡器閒置時，把清單中顯示中卡片的下一張以載入時的條件開卡，再隱藏建好角色（`OKGalleryNeighbor.cpp`）。載入該卡時直接換上，只跑 FinishPreparedPreview。它排在穿脫變體預建之前。 |

原生量測（`ok-card-open-bench`，各 4 次取最小，同機另有遊戲與模擬器，CPU 約 74%）：

| 變體 | C044 | C058 | C087 | 卡片增加 |
|---|---|---|---|---|
| 原本（PNG） | 3.85 s | 3.27 s | 3.08 s | — |
| 原始貼圖 | 2.96 | 2.87 | 2.77 | — |
| memo tex | 2.34 | 1.98 | 2.09 | +24–36 MB |
| memo res | 3.64 | 3.22 | 3.10 | +3 MB |
| memo mesh | 3.04 | 2.80 | 2.62 | +13–23 MB |
| memo 全部 | 2.03 | 1.63 | 1.68 | +34–46 MB |
| 烘焙（第一套，LZ4） | 0.10 | 0.17 | 0.11 | +21–29 MB |
| 烘焙（第一套，zstd 3） | 0.10 | 0.16 | 0.12 | +17–25 MB |
| 烘焙（全部服裝） | 0.14 | 0.22 | 0.14 | +26–87 MB（4 套 26 MB，7 套 50–69 MB） |

載入時間與卡片大小總表：
- 秒數：「原生開卡」是量測工具 C044／C058／C087 的平均；讀卡器四欄是遊戲內 C030–C033 的實測。
- 大小：同一組四張測試卡的平均，原本 15.5 MB。
- 「—」表示沒做遊戲內實測（量測工具已顯示不如同類變體）。

| 變體 | 原生開卡 | 讀卡器第一張 | 依序下一張 | 換裝 | 再開 | 卡片大小 | 倍數 |
|---|---|---|---|---|---|---|---|
| 原本 | 3.40 | 6.73 | 6.89 | 3.62 | 3.41 | 15.5 MB | ×1.00 |
| 原始貼圖 | 2.87 | 7.44 | 6.77 | 4.48 | 2.68 | 15.5 MB | ×1.00 |
| 材質共用 | 3.40 | 5.82 | 6.81 | 3.24 | 1.60 | 15.5 MB | ×1.00 |
| 部件預算 250 ms | 3.40 | 6.90 | 7.22 | 3.19 | 2.76 | 15.5 MB | ×1.00 |
| 鄰卡預建 | 3.40 | 7.96 | 1.25 | 4.44 | 4.37 | 15.5 MB | ×1.00 |
| memo tex | 2.14 | — | — | — | — | 47.9 MB | ×3.10 |
| memo res | 3.32 | — | — | — | — | 18.5 MB | ×1.20 |
| memo mesh | 2.82 | — | — | — | — | 30.0 MB | ×1.94 |
| memo 全部 | 1.78 | 6.25 | 5.93 | 3.84 | 3.22 | 60.4 MB | ×3.91 |
| 烘焙第一套（LZ4） | 0.13 | 3.61 | 2.93 | 4.02 | 3.00 | 45.6 MB | ×2.95 |
| 烘焙第一套（zstd 3） | 0.13 | — | — | — | — | 41.8 MB | ×2.70 |
| 烘焙第一套（原始貼圖） | 0.14 | — | — | — | — | 52.7 MB | ×3.41 |
| 烘焙全部服裝 | 0.17 | 3.91 | 3.19 | 2.01 | 2.85 | 66.7 MB | ×4.32 |
| 烘焙全部＋材質共用＋預算 | 0.17 | 2.58 | 2.65 | 1.63 | 1.63 | 66.7 MB | ×4.32 |
| 不增大卡片：原始貼圖＋材質共用＋預算＋鄰卡 | 2.87 | 7.10 | 1.06 | 3.49 | 2.01 | 15.5 MB | ×1.00 |
| 全部（烘焙全部＋原始貼圖＋材質共用＋預算＋鄰卡） | 0.15 | 3.16 | 0.73 | 1.36 | 1.95 | 78.2 MB | ×5.06 |

卡片大小與原本新版 OK 卡比較（括號為倍數）：

| 變體 | C030 | C044 | C058 | C087 | 平均倍數 |
|---|---|---|---|---|---|
| 原本 OK 卡 | 7.1 MB | 6.5 MB | 32.9 MB | 8.8 MB | ×1.0 |
| 烘焙第一套（LZ4） | 31.8（×4.5） | 35.2（×5.4） | 62.0（×1.9） | 34.9（×4.0） | ×4.0 |
| 烘焙第一套（zstd 3） | 28.4（×4.0） | 31.1（×4.8） | 59.3（×1.8） | 32.5（×3.7） | ×3.6 |
| 烘焙第一套（原始貼圖） | 37.8（×5.4） | 41.8（×6.5） | 66.3（×2.0） | 33.9（×3.9） | ×4.4 |
| 烘焙全部服裝 | 33.0（×4.7） | 75.6（×11.7） | 93.4（×2.8） | 59.8（×6.8） | ×6.5 |
| 烘焙全部服裝（原始貼圖） | 39.0（×5.5） | 93.0（×14.3） | 106.0（×3.2） | 60.2（×6.8） | ×7.5 |
| memo tex | 32.5（×4.6） | 35.1（×5.4） | 69.3（×2.1） | 32.9（×3.7） | ×4.0 |
| memo res | 10.1（×1.4） | 9.5（×1.5） | 36.0（×1.1） | 11.9（×1.3） | ×1.3 |
| memo mesh | 23.6（×3.3） | 21.7（×3.3） | 56.1（×1.7） | 21.9（×2.5） | ×2.7 |
| memo 全部 | 46.2（×6.6） | 47.3（×7.3） | 79.2（×2.4） | 43.2（×4.9） | ×5.3 |

遊戲內測試卡（C030 7.1、C031 21.6、C032 26.1、C033 7.0 MB）：烘焙第一套 31.8／51.2／69.4／29.8 MB（×2.4–4.5），烘焙全部服裝 33.0／84.4／87.2／62.1 MB（×3.3–8.8），再加原始貼圖 39.0／96.0／101.7／76.2 MB（×3.9–10.8），memo 全部 46.2／45.5／108.3／41.6 MB（×2.1–6.6）。增加的量大致固定（一套服裝約 20–40 MB），所以本來就帶卡包的大卡（C058、C031、C032）倍數較小。

遊戲內（讀卡器 `-game`，空快取、不開預熱行程，序列 `C030,~8,C031,~8,C032,~8,C033,~8,@1,~8,C030`，每變體 2 次平均，同樣負載，單位秒）：

| 變體 | 第一張 C030 | 下一張 C031–C033 平均 | 換裝 @1 | 再開 C030 |
|---|---|---|---|---|
| 原本 | 6.73 | 6.89 | 3.62 | 3.41 |
| 原始貼圖 | 7.44 | 6.77 | 4.48 | 2.68 |
| 材質共用 | 5.82 | 6.81 | 3.24 | 1.60 |
| 部件預算 250 ms | 6.90 | 7.22 | 3.19 | 2.76 |
| 鄰卡預建 | 7.96 | 1.25 | 4.44 | 4.37 |
| memo 全部 | 6.25 | 5.93 | 3.84 | 3.22 |
| 烘焙第一套 | 3.61 | 2.93 | 4.02 | 3.00 |
| 烘焙全部服裝 | 3.91 | 3.19 | 2.01 | 2.85 |
| 烘焙全部＋材質共用＋預算 | 2.58 | 2.65 | 1.63 | 1.63 |
| 不增大卡片：原始貼圖＋材質共用＋預算＋鄰卡 | 7.10 | 1.06 | 3.49 | 2.01 |
| 全部（烘焙全部＋原始貼圖＋材質共用＋預算＋鄰卡） | 3.16 | 0.73 | 1.36 | 1.95 |

結論：
- **烘焙是首次載入最有效的一項**：原生 3–4 s → 0.1–0.2 s，讀卡器 6.7–6.9 s → 2.6–3.6 s。代價是卡片每張多約 20–90 MB，存全部服裝也只比第一套多一些，而且換裝同樣變快。memo 系列在大小與速度上都不如烘焙；其中只有 res 很小（+3 MB），但只省約 0.15 s。
- 原始貼圖原生省 0.2–0.9 s，但遊戲內被貼圖準備與傳輸抵銷，效益不明顯，而且快取每筆多約 16%。
- 材質共用以來源內容比對，同卡內就有約六成命中，再開同一張卡約九成；再開快 1.0–1.8 s，換裝也快。簽章每張卡約 0.05–0.19 s。
- 預算加大對換裝與再開有幫助（約 0.4–0.6 s），首次載入沒有差別，因為那時瓶頸是原生開卡。
- 鄰卡預建：依序瀏覽時下一張 0.2–0.7 s。重卡（C032）8 秒閒置不夠建完，但開卡快取已在背景補好，仍降到 2–4 s。代價是多佔一個角色的記憶體，而且穿脫變體延後，所以換裝和回到前一張沒有加速。
- 烘焙後一張卡剩下約 2.6 s，大約是：工作執行緒 0.85 s（其中貼圖準備 0.5–1.0 s）、UE 部件 1.4 s（材質 0.25–0.63 s）、收尾 0.45 s（臉手 0.1–0.2 s、動畫目錄 0.04–0.25 s）。
- 31 次遊戲內實測中有一次當機（`Saved/Crashes/UECC-Windows-B5EABC28…`，TaskPrivate.cpp:80「Deadlock detected」，Foreground Worker #0，呼叫堆疊經過 Renderer／RenderCore）。發生在第一張卡載入完 1 秒後，當時正在跑漸進式材質替換（pending 129→112），穿脫變體預建也剛開始。這輪新增的程式當時都沒有作用：材質共用與原始貼圖是關的，鄰卡預建在那一版不支援卡片資料夾模式，log 裡沒有任何一行 `OK neighbor`。
  - 用 WinDbg 附的 cdb 解 minidump，堆疊全部在引擎裡：`FRDGBuilder::FAsyncDeleter` 的 task 釋放 RDG 配置 → `FSceneExtensionsUpdaters::End` → `Nanite::FEditorSceneExtension::FUpdater::End()` 等待非同步建置繪製清單的 task → task 系統判定 deadlock。
  - 這個 scene extension 只存在於編輯器建置，讀卡器是 `UnrealEditor -game` 所以會遇到。
  - 處理：`Config/DefaultEngine.ini` 的 `[ConsoleVariables]` 設 `r.Nanite.Editor.DrawLists.AsyncUpdates=0`，改在 render thread 上直接建，沒有 task 可等；角色是骨架網格，不受影響。
  - 解法：`cdb -z UEMinidump.dmp -y "srv*<cache>*https://msdl.microsoft.com/download/symbols;D:\PhysX\UE5.8\Engine\Binaries\Win64" -c ".symopt+0x40;.ecxr;kn 50"`，再用 `dps <物件位址>`＋`ln <vtable>` 認出物件類別。

實測工具：`ok-card-bake`、`ok-card-open-bench`（含 `--compare`）。遊戲內用 `OK_LOAD_PROFILE=1`，`OKCARD_PROFILE progressive` 列多了 `dedupe_hits/misses/ms`，鄰卡另有 `OK neighbor preload worker/ready/hit` 記錄。

### 第一張卡（2026-10-17 第六輪，預設啟用，輸出不變）

讀卡器剛啟動後載入的第一張 OK 卡，逐段拆解後處理每個行程「第一次才有」的成本，以及重複做的工作：
- **替代材質預熱**：漸進式材質用的三個替代材質（`M_Preview*`），原本要等它們的 shader map 才能進入收尾，第一張卡空等 13 幀、0.38 s。讀卡器閒置時先載入並送出編譯（`AKKPreviewActor::PrewarmPreviewMaterialParents`）。
- **共用動畫清單預熱**：OK 卡的動作選單要列出匯入的共用片段，第一次列出會在遊戲執行緒同步載入這些資源，約 0.27 s。改在閒置時先載入（`FOkSession::ImportedSharedClipNames`），量測為 0.27 s → 0.4 ms。
- **貼圖準備**：不再先在單一執行緒逐張做 SHA1 並複製。改為就地讀取 session 的位元組，以「長度相同再逐位元組比對」合併同內容的貼圖（80 張，避免重複解碼）；大張優先、每張一個工作項目平行解碼；解碼後的像素直接成為 mip 頂層，不再複製（`OKPrecacheRuntimePngView`、`ExtendEncodedMipChain`）。解碼部分約 0.25 s。
- **縮圖解碼略過資料 chunk**：選卡時的縮圖解碼略過 `okLc`（卡包）、`okBk`、`okMm`，不再讓 libpng 讀過、檢查數十 MB 的資料。
- **手勢不重算**：載入收尾會把左右手設回「跟隨動作」，原本因此整個姿勢重算一次（約 0.15 s）。關閉一個本來就沒開的手勢覆寫不會改變任何骨骼，現在直接略過（`AKKPreviewActor::SetHandShape`）。
- **mod 與體型一次套用**：載入時先套卡片的 mod，骨骼 mod 一變就用 session 原本的體型值重建一次體型；接著再用卡片的體型值重建一次，各約 0.12 s。卡片的體型值確實和 session 不同（身體 5–7 個滑桿，最多差 1.4），所以第二次一定要做，第一次則是多餘的。新增 `ok_editor_apply_mods_shape`（`editor_mods::apply_with_shape`，UE 端 `FOkSession::ApplyEditorModsAndShape`）：孕肚設定不會改變任何頂點時，只設骨骼 mod，體型用卡片值建一次；有孕肚要套時照原順序。ignored 測試 `mods_then_shape_once_equals_two_steps` 用 C030、C031、C033、C044 各兩組滑桿驗證，結果和原本兩步逐位元組相同。
- `OK_LOAD_PROFILE=1` 的 `OKGUI_PROFILE` 收尾步驟拆得更細（`face_*`、`animation_*`），工作執行緒另印 `OKGUI_WORKER physics/mods/shape` 與 `OK texture precache`。

實測（嵌入全部服裝＋材質共用＋預算的卡 C030，同機 CPU 54–93%，有其他 session 的遊戲與 cook 在跑）：
- 第一張：2.79 s → 2.47／2.76／2.67 s，`OKGUI_LOAD` 2.34–2.61 s。
- 各段：替代材質空等 0.38 s → 0；`animation_clip_sources` 0.27 s → 0.4 ms；`face_hand` 0.15 s → 0.4 ms；貼圖輸入 247 → 80 張，解碼約 0.25 s；mod＋體型 0.25 s → 0.17–0.19 s。
- 總時間省得比各段加總少，是因為同時段負載較高，部件建構和材質變慢了（材質 0.41 s → 0.58 s）。
- 原本的卡（無實驗開關）同時段 7.75–8.07 s，高負載下無法和先前的 6.7 s 直接比較。

剩下的主要成本：材質建立 0.4–0.6 s（含 finalize 0.15–0.23 s）、部件貼圖建立 0.25–0.34 s、工作執行緒 0.5 s（貼圖解碼 0.25–0.28 s、mod＋體型 0.17–0.19 s）、建立替代材質並送出編譯 0.25 s、每張卡重建共用骨架 0.08–0.14 s。

### 新版 OK 卡未快取首載（2026-10-18 第七輪，`OK_FIRSTLOAD`；A2、A3、B1、C3、C1 預設開啟）

只看「讀卡器新開、開卡快取是空的、第一次載入一張新版 OK 卡」。開關 `OK_FIRSTLOAD=A2,A3,B1,C2,C3`、`all` 或 `none`（UE 端 `AKKPreviewActor::FirstLoadOption`、Rust 端 `first_load_option`，各讀一次）。沒設時等於 `A2,A3,B1,C3`（Rust 端預設 A2、A3，UE 端預設 B1、C3）；設了就整個取代預設。C1 是 `OK_MATERIAL_DEDUPE`，預設開，設 `0` 關閉。A3 的「卡包新增的檔案贏得項目」這條路徑，手邊的卡都跑不到（`added_zips=0`），改用 `list::tests::layered_view_equals_full_merge` 驗證：隨機產生 200 組檔案與清單，分層目錄在 get、get_slot、項目數上都和整份合併相同。

| 代號 | 做法 | 位置 |
|---|---|---|
| A1 | 身體／臉／道具三段平行 | 沒做：道具需要身體鏈的輸出（body_texs、head_full_skel），臉和身體共用 `seen_meshes`、`authored_fbs` 的可變狀態 |
| A2 | 一張卡開啟時的 `push_meshes` 呼叫先記下（`PushBatch`），等所有道具、頭髮、服裝都走完，再一起平行跑每個網格的骨架與貼圖轉換，最後照呼叫順序輸出 | lib.rs `PushCtx`、`PushBatch`；細節見下方「A2 第二版」 |
| A3 | 掛上卡包後不重新合併整份 mods 目錄，沿用預熱時的目錄，只另存卡包新增檔案贏得的項目（分層目錄） | list.rs `layered_catalog`、`ChaListCatalog::base` |
| B1 | 貼圖解碼和 mod／物理在工作執行緒上同時跑 | OKGalleryWidget.cpp StartLoad3D |
| C1 | 材質共用（既有開關） | KKMaterialDedupe.cpp |
| C2 | 貼圖用 `CreateTransient` 帶入 mip0，只補其餘層 | KKTextureRuntime.cpp `CreatePackedTexture` |
| C3 | 先顯示替代材質，最終材質的編譯每幀送出約 3 ms | KKPreviewLoading.cpp |
| C4 | 打包版讀卡器走預先 cook 的共用材質（不需要執行期編譯） | KKPreviewRuntimeMaterials.inl，見下 |
| C5 | 打包版材質準備（解碼＋mip）各貼圖平行 | KKRuntimeMaterial.cpp `OKCaptureRuntimeMaterials` |
| C6 | 打包版 cooked 部件不再從 PNG 建貼圖、不做 UE 端預解碼 | KKPreviewActor.cpp `BuildSessionPartRange`、`PrecacheTextureJobs` |

原生輸出驗證：`ok-card-open-bench --compare`（新增 `OK_BENCH_PREWARM=1` 先跑讀卡器的預熱；`OK_BENCH_DUMP=<dir>` 寫出兩份位元組）在 A2＋A3 下，C030–C033 與基準逐位元組相同。

遊戲內實測（`UnrealEditor -game`，每次新行程＋空快取，順序 C030 → C032，變體交錯，各 3 輪，base／C1／best 6 輪；同機有其他 session 的建置與遊戲，CPU 負載變動很大）。「載入」= `OKGUI_LOAD`（看得到角色），「最終」= 從開始載入到最後一個最終材質換上；「UE 端」= 減去工作執行緒原生開卡時間，比較不受負載影響。都是中位數（括號內為最小值），單位秒：

| 變體 | C030 載入 | C030 最終 | 原生開卡 | UE 端載入 | UE 端最終 | 貼圖建立 | 送出編譯 | C032 載入 | C032 最終 |
|---|---|---|---|---|---|---|---|---|---|
| base | 6.52 (5.74) | 9.57 (8.71) | 3.82 | 2.70 | 5.75 | 0.35 | 0.69 | 9.27 (7.95) | 11.29 (9.39) |
| A2 | 7.62 (6.96) | 10.80 (9.95) | 4.60 | 3.02 | 6.09 | 0.41 | 0.70 | 11.24 (10.19) | 12.70 (11.69) |
| A3 | 6.15 (4.54) | 9.15 (7.50) | 3.79 | 2.16 | 5.16 | 0.21 | 0.64 | 7.44 (7.33) | 8.81 (8.74) |
| B1 | 5.71 (5.55) | 8.91 (8.43) | 3.49 | 2.35 | 5.43 | 0.31 | 0.63 | 8.36 (7.94) | 9.77 (9.27) |
| C1 | 5.33 (4.98) | 6.76 (6.28) | 3.56 | 1.79 | 3.16 | 0.25 | 0.24 | 8.85 (8.44) | 10.15 (9.63) |
| C2 | 6.48 (4.77) | 9.61 (8.24) | 3.97 | 2.46 | 5.64 | **0.48** | 0.53 | 9.37 (6.98) | 10.73 (8.26) |
| C3 | 6.29 (5.41) | 10.06 (9.41) | 4.74 | 1.73 | 5.73 | 0.23 | 0 | 10.51 (7.92) | 12.57 (9.77) |
| all（A2,A3,B1,C2,C3＋C1） | 7.26 (4.13) | 9.12 (5.84) | 4.71 | 2.54 | 4.41 | **1.11** | 0 | 11.05 (7.26) | 12.95 (8.78) |
| **best（A3,B1,C3＋C1）** | **4.58 (4.34)** | **6.28 (6.14)** | 3.31 | **1.23** | **2.94** | 0.19 | 0 | 8.89 (8.14) | 10.68 (9.83) |

結論：
- 有效：**C1** 讓最終材質提早最多（UE 端最終 5.75 → 3.16 s）；**C3** 把送出編譯的 0.6–0.7 s 從首次顯示移開（UE 端載入 2.70 → 1.73 s），但最終時間不變；**B1** 讓貼圖解碼和 mod 重疊（約 −0.3 s）；**A3** 讓分層目錄只花 8 ms，原生端約 −0.1～0.2 s。
- 無效：**A2** 沒有可量到的效益：每次呼叫的網格少、貼圖轉換本來就不是瓶頸。**C2** 反而讓貼圖建立變慢（C032 0.67 → 1.38 s），這也是 `all` 輸給 `best` 的原因。
- 讀卡器第一張（C030）：best 讓載入 6.52 → 4.58 s（−30%）、最終 9.57 → 6.28 s（−34%）。第二張新卡（C032）的原生開卡占大頭（6–7 s），省得較少（載入 9.27 → 8.89 s、最終 11.29 → 10.68 s）。

改成預設後（未設任何開關），同樣的量測跑兩次：C030 載入 4.11／3.76 s、最終 5.84／5.32 s；C032 載入 6.99 s、最終 8.54 s（另一次原生開卡被負載拖到 13.5 s，不列入）。

#### A2 第二版：跨呼叫批次與 sRGB 查表（2026-10-18）

第一版只在單次 `push_meshes` 呼叫內平行，量不到效益。原因是一次呼叫平均只有 1.8 個網格，C032 的 25 次呼叫都是這樣。

改法：
- **跨呼叫批次**：每次呼叫先在原處做過濾與去重（`seen_meshes`），把輸入存成 `PushCtx`。等整張卡的道具、頭髮、服裝、副服裝都走完，`PushBatch::flush` 一起平行跑全部網格的骨架與貼圖轉換，再照呼叫順序輸出。
- **骨架快照**：服裝那一輪會改到身體骨架（衣物動態加骨），所以批次裡的呼叫保存當時的身體與頭部骨架副本。
- **其他執行緒狀態**：`ACC_PLACE` 改成 `rig_mesh` 的明確參數；衣物狀態和 profile 覆寫在工作執行緒上重新套用。
- **sRGB 查表**：法線圖轉換和「線性轉 sRGB」原本每個通道都呼叫一次 powf。輸入只有 8 位元，改成 256 格的表，結果逐位元組相同。C032 的 RoadKillOut 法線圖從 960 ms 降到 365 ms。

驗證：`ok-card-open-bench --compare` 比對 24 張卡，預設與 A2 都和基準逐位元組相同：
- C030–C033
- StreetHeroines 6 張
- CrowdAPNG 6 張
- 另外從 KKS 轉的 8 張新 OK 卡

`OK_A2_TRACE=1` 會印出每個網格的耗時。

遊戲內首載（未快取，交錯 3 輪，中位數）：

| | C030 載入 | C030 最終材質 | C032 載入 |
|---|---|---|---|
| 原預設（A3,B1,C3＋C1） | 3.36 s | 5.58 s | 4.96 s |
| 原預設＋A2 | 3.11 s | 4.77 s | 4.61 s |

所以 A2 也改為預設。查表本身在原生端讓 C032 從約 3.9 s 降到約 3.4 s，兩種設定都有這個效果。

A3 的「卡包新增檔案」路徑：從 KKS 卡轉出的 8 張新 OK 卡，卡包最大 5 MB，掛上後一樣是 `added_zips=0`。OK 卡帶的 mod 內容是以錄好的資源形式提供，不會變成新的 zip，所以實際開卡不會走到那條路徑；那條路徑只靠上面的隨機測試驗證。

#### 切換動畫（2026-10-18）

`OK_AUTO_SEQUENCE` 新增兩種步驟：`#N`／`#文字` 選動作選單第 N 項，或第一個包含該文字的項目；`#?` 列出所有選項。

C030 的動作選單有 744 項。量了 Demo（基本、蹦跳、站立、坐姿、躺姿、特殊、H、男性）和 Studio 片段：
- 每次切換從選擇到套用完成約 15–40 ms。
- 第一次用某個控制器也一樣快：準備 4–8 ms、套用 6–11 ms。
- 只有一次「蹦跳」用了 0.46 s；在新行程裡重測同一個首次切換只花 4 ms，那次是機器負載造成的。

新版 OK 卡的切換動畫不需要再優化。

#### C4：打包版讀卡器走 cooked 共用材質（2026-10-18 第八輪）

打包版讀卡器要帶兩個參數：
- `RuntimeMaterialRoot=/Game/IndependentResources/RuntimeMaterials/StreetDisplay24`（當時是 StreetDisplay23）
- `-pakdir=<專案>/Saved/StreetSocial/Build/StreetDisplay24HG.patch`（當時是 StreetDisplay23HG.patch）：街頭用的 patch，`rebuild_street_social_patch.lua` 產生。

不需要新的材質根目錄，也不需要另外 cook：街頭的母材質根目錄（當時 StreetDisplay23，現在 StreetDisplay24）本來就有全部 Koikano 母材質（60 個），patch 裡也有。先前回報「缺 `M_KoikanoSkin`」，其實是沒帶 `-pakdir`，patch 根本沒掛上。

掛上 patch 之後，原本還缺 shader：cooked 母材質的 shader 在 patch 自己的 library `RuntimeMaterialBake` 裡，引擎不會自動開，只有街頭場景會依 manifest 開。現在 `OKPrepareCookedRole` 第一次使用時會開這個 library，依序取：
1. `RuntimeMaterialLibrary=`；
2. manifest 的 `material_library`；
3. 預設 `RuntimeMaterialBake`。

**打包版貼圖準備慢的原因**（`texture_prepare` 2.1–2.8 s，編輯器版 0.4–0.5 s）：打包版的 `CollectTextures`（`#if !WITH_EDITOR`）會先在工作執行緒跑 `FOkSession::PrepareRuntimeMaterials` → `OKCaptureRuntimeMaterials`。它逐張貼圖、循序做這三件事：
- SHA1；
- PNG 解碼；
- 用 double 精度 box filter 建完整 mip 鏈，每個像素都做 sRGB→linear。

這段 C030 要 1.9–2.2 s、C032 要 3.4–3.6 s，編輯器版沒有。新增 **C5**（`OK_FIRSTLOAD=C5`）：先讀完所有部件，各張不同的貼圖再以 `ParallelFor` 平行準備，貼圖順序和預算檢查不變。打包版另外輸出 `OK runtime material capture ms=` 這行 log。

實測條件：
- 執行檔：`Saved/StreetSocial/Build/CardReaderC4_01.cardstage`。自己的 host `Saved/Interaction/CardReaderHostD`，D: 引擎。副檔名不是 `.stage`，所以 StreetSocial.cmd 不會選到。
- 每次都是新行程＋空快取，序列 `~12,C030,~12,C032,~12`，變體交錯各 3 輪。
- base = `OK_FIRSTLOAD=none`；best = `A3,B1,C3`＋`OK_MATERIAL_DEDUPE=1`。

cooked 路徑沒有執行期編譯，「載入」就是「最終」。都是中位數（括號內為最小值），單位秒：

| 變體（打包版） | C030 載入＝最終 | 原生開卡 | 工作執行緒貼圖準備 | 其中材質準備 | UE 端載入 | C032 載入＝最終 | C032 材質準備 |
|---|---|---|---|---|---|---|---|
| base | 7.10 (6.55) | 3.76 | 2.35 | 1.98 | 3.34 | 12.09 (11.96) | 3.57 |
| best | 7.01 (6.64) | 3.62 | 2.33 | 2.05 | 3.40 | 11.17 (10.75) | 3.36 |
| **best＋C5** | **5.02 (4.87)** | 3.42 | **0.58** | **0.34** | **1.60** | **10.27 (8.06)** | **0.57** |

和編輯器 best 比較：

| | 編輯器 best | 打包 best＋C5 | 變化 |
|---|---|---|---|
| C030 載入 | 4.58 | 5.02 | +0.44 s |
| C030 最終 | 6.28 | 5.02 | **−1.26 s（−20%）** |
| C032 最終 | 10.68 | 10.27 | −0.41 s |

C030 的畫面（髮色、服裝、描邊）正確。另外，Template 地圖的天空、格子材質缺 shader，這是原本就有的狀況，和角色無關。

C5 只影響打包版（編輯器讀卡器不跑 `OKCaptureRuntimeMaterials`），街頭場景的群眾材質準備也走同一個函式，但預設不開。

#### C7：編輯器／-game 也走共用母材質（2026-10-19）

使用者反映冷讀卡一直在編 shader。原因是編輯器路徑（讀卡器 `CardReaderDevelopment.cmd`、圖庫、OK Studio、角色編輯都經過 `AKKPreviewActor`）每個部件都 `NewObject<UMaterial>`。卡片的顏色、`_ST`、線寬、陰影色和所有 Unity uniform 都被寫死成 graph 常數，或寫成 Custom HLSL 裡的 `asfloat(0x…)` 字面值，所以每張新卡都會編一批自己的 shader。`OK_MATERIAL_DEDUPE` 只對同一張卡有效。

現在改成在每種版本都用共用母材質的實例（`OKSharedParents()`）：
- **預設**：編輯器與 `-game` 預設開啟，`OK_SHARED_PARENTS=0` 可退回舊路徑；打包版一律開啟。
- **母材質根目錄**：編輯器預設 `/Game/IndependentResources/RuntimeMaterials/StreetDisplay24`，HG 風格用 StreetHG03（2026-10-19 前是 StreetDisplay23／StreetHG02）。也可以用 `RuntimeMaterialRoot=` 或 manifest 指定。
- **著色器**：編輯器載入的是未 cook 的母材質，同一台機器只編一次，之後放在 DDC；不開 cooked shader library。
- **沒有母材質的部件**：沒有對應家族（recipe None）或建立實例失敗時，編輯器改用這個部件自己的 graph，記在 log 的 `own_graph=N`；打包版一樣拒絕整張卡。
- **跳過的舊材質**：有共用材質的部件不再建 native skin 材質，也不再建 overtex 網格段（共用程式自己會畫 `_overtex`）。

踩過的坑：外框殼的三角形繞向。編輯器自己的外框 graph 要反向繞向（`AddOutlineSection` 的 `C,B`，以及 `KKSkeletalBuilder` 的 `ReverseOutline`）；共用 `_Outline` 母材質和打包版一樣，要原始繞向，因為程式自己 cull 正面。第一版沒改這裡，外框殼的外側面整個蓋住身體，角色變成黑色剪影（半透明的眼睛和褲子正常）。現在：
- `AddOutlineSection` 依部件有沒有共用材質決定繞向（`bSourceWinding`）。
- `OKBuildSkeletalMesh` 多一個 `bSourceWinding` 參數，也算進網格快取的 key。

驗證：
- **建立數量**（惠惠，editor -game）：`OKCARD_PROFILE materials=0 material_ms=0.0 own_graph=0`，每種服裝都一樣。
- **對照組**：`OK_SHARED_PARENTS=0` 同一張卡 `materials=56/48 material_ms=118–128`。
- **截圖**：新舊路徑同角度截圖幾乎一樣，也和打包版讀卡器（CardReaderC4_03）一致。

第二個坑（讀卡器 session 回報，jeanne.png 的頭髮在 C7 變成黑紫色，舊路徑是金髮）：
- **原因**：這頂頭髮是 Forge item（`Shader Forge/main_item`），共用母材質沒有給這個家族 `_Outline`，`MakeOutlineMaterial` 回傳 null。但 `AddOutlineSection` 已經先建好外框網格段，沒有材質就畫成引擎預設材質，整個蓋在頭髮上。打包版也會這樣。
- **修正**：先取外框材質；共用路徑的部件如果沒有外框，就不建外框殼。程序網格（`AddOutlineSection`）和骨架網格（`SkelOutline`、臉的外框殼）都一樣。
- **查的過程中一併修正**（`KKRuntimeMaterial.cpp` 的擷取）：
  - `_RampG` 改用角色的 ramp（`V.ramp_png`），不再被材質自己殘留的 ramp 蓋掉，和舊路徑、原作一致。
  - Item 類程式（kind 2／8／9／11）的 `_MainTex` 先取它自己的 shader 貼圖。
  - 灰階 PNG 明確展開成三個色版。
  - 預備貼圖快取鍵升級為 `mips-v4`。
- **診斷工具**：`OK_SHARED_DUMP=<部件名片段>` 會把共用路徑和舊路徑交給該部件的參數、貼圖（平均色、原始尺寸）都寫進 log，也會記每個外框殼用的繞向。
- **驗證**：jeanne.png 共用路徑頭髮像素 (248,235,193)，和舊路徑一樣；大腿的深色帶也一起消失。惠惠沒有變化。

第三個坑（同一頂頭髮，修好顏色後少了黑色輪廓線和髮絲線，變成一整片平塗）：
- **原因**：上一個修正的歸因錯了。`M_ForgeItem_Outline` 其實存在；外框材質沒建出來，是因為擷取時 `Part.Outline` 只看 `_OUTLINE_ON` keyword（另外只對 kind 1／10 例外打開）。舊路徑（`bOutlinable`）只有 kind 2／3／9 看 keyword，其他家族一律畫外框。這頂 Forge main_item 頭髮沒有存 keyword，所以共用路徑判定它沒有外框，舊路徑則畫了。
- **修正**：擷取時 kind 2／3／9 以外都設 `Outline=true`，和舊路徑一致。街頭（`OKCrowdRuntimeScene`）用同一個欄位，快取鍵也含這個欄位，會自動重建。上一個修正保留下來作為保險：真的沒有外框母材質時不建外框殼。
- **另一個坑**：`Saved/Interaction/CardReaderHostD` 的 Plugins／Source 是複製的，不是 junction。改了原始碼要先跑 `Tools/prepare_interaction_runtime_host.lua --host-path <host>` 再編譯；讀卡器還開著時 DLL 會被鎖住，連結會失敗。
- **驗證**：jeanne.png 共用路徑頭髮恢復輪廓線和髮絲線，顏色不變；`OK_SHARED_DUMP` 可以看到每個外框殼用的材質（`OK outline material`）和外框綁定（`OK shared outline dump`）。

#### 外框粗細改用原作頂點程式（2026-10-19）

共用 `_Outline` 母材質的像素程式本來就是從原作 DXBC 轉的，但外框殼往外推多少（線的粗細）是手寫公式：`(d·0.001+0.3)×__OK_OutlineWidth×0.5`（Koikano 是 `(d·0.001+0.6)×…×0.3`），頭髮再加一個「至少 4 像素」的下限。現在推出量改成直接跑原作外框 pass 的頂點程式：
- **產生**：`Tools/generate_runtime_outlines.lua` 也把每個家族外框 pass 的頂點程式轉成 HLSL（`G<家族>OutlineVertexProgram`），連同它自己的 CB 配置、uniform、貼圖表輸出到 `OutlinePrograms.inl`。頂點程式如果用到 `_WorldSpaceCameraPos`、`unity_ObjectToWorld`／`unity_WorldToObject`／`unity_MatrixVP` 以外的引擎值，產生器會直接報錯。
- **執行**：`PrepareRuntimeSharedMaterial` 的外框 WPO 以 Unity 世界座標餵入頂點程式（位置、法線、UV0、鏡頭位置；物件與 VP 矩陣用單位矩陣），輸出位置減掉輸入位置就是推出量。所以 `saturate`、`_linewidthG`、`_LineWidthS`、DetailMask／AlphaMask 的遮罩、`_alpha_a`／`_alpha_b` 等都和遊戲一樣。
- **參數**：頂點程式讀的材質值（例如 `_linewidthG`、`_LineWidthS`）由 `OKRuntimeOutlineLayout` 補進外框 layout，和像素程式的參數一樣綁定。
- **可選的舊算法**：`ok.OutlineApprox 1`（或環境變數 `OK_OUTLINE_APPROX=1`，綁定材質實例時讀取）改回手寫公式和頭髮下限。預設 0，也就是原作。
- **母材質**：新的根目錄是 `StreetDisplay24`／`StreetHG03`，編輯器讀卡器預設用它們。街頭打包版也已換成這兩個根目錄（`Launch/materials.json`，patch `StreetDisplay24HG.patch`）；臉部母材質（22 個 `_Fg`、`M_OKTearLit`）用 `prepare_face_materials.lua` 在新根目錄產生。街頭驗證（`Outline24.stage`，Koikatsu 與 HG 兩種風格）：每位女主的 `Runtime source outlines` 數量和 StreetDisplay23 完全相同，`Runtime foreground eye/brow parents: 22`、淚滴用 `M_OKTearLit`，沒有部件因缺母材質被拒；同角色截圖的線條粗細與舊版相近，略細。
- **舊母材質相容**：StreetDisplay23 以前的外框母材質沒有頂點程式的輸入，也沒有 `__OK_OutlineApprox`。`OKCreateRuntimeSharedMaterial` 只對這些參數放寬「母材質缺參數就失敗」的檢查，其他參數仍然嚴格檢查。
- **診斷**：`OK_OUTLINE_DUMP=1` 會對每個外框實例輸出 `OK outline instance <部件> recipe=… parent=… __OK_OutlineApprox=… _linewidthG=… _LineWidthS=…`。
- **驗證**（jeanne.png，CharacterMCP T-pose，頭部區域深色線條像素，正面／側面）：原作頂點程式 899／780，`OK_OUTLINE_APPROX=1` 為 899／779（main_item 的手寫公式本來就和原作相同），舊路徑 `OK_SHARED_PARENTS=0` 為 828／717；三者頭頂皮膚像素都是 0。惠惠兩種模式只差 11 像素；log 確認近似模式下每個外框實例都綁上了 `__OK_OutlineApprox=1`。
- **遠景**（惠惠，Forge 頭髮，1920×1080 透視 30°，3／8／15 m）：深色線條像素原作 7470／3500／1058，近似（含頭髮至少 4 像素的下限）7474／3572／1111。15 m 時原作的頭髮外框仍有 1–2 像素寬，沒有斷線，所以預設不需要像素下限。

#### 配件動態骨骼套用 DynamicBoneEditor（2026-10-19）

卡片的 `com.deathweasel.bepinex.dynamicboneeditor`（`AccessoryDynamicBoneData`）會依服裝、配件 slot、DynamicBone 的 m_Root 名稱改 FreezeAxis、Weight、Damping、Elasticity、Stiffness、Inertia、Radius。現在開卡時會套到配件的動態骨骼（`apply_dynamic_bone_editor`）。Weight 依 UpdateParticles2 的 `Lerp(1, stiffness, weight)` 寫進剛性及其分布曲線，因為 `DBone` 的結構不能改（OK 函式庫紀錄是 bincode）。jeanne 的辮子（Damping 0.33、Elasticity 0.047）在讀卡器裡下垂了，原卡沉降後和遊戲 dump 的界限差 3 mm 以內。診斷：`OK_DBONE_DIAG=1`，以及 `crowd_native_tests::card_part_settle`。

#### C6：打包版去掉重複的貼圖工作

best＋C5 下，cooked 路徑還有兩段工作和 cooked 材質已準備好的貼圖重複：
- 部件建構時又從 PNG 建一次部件貼圖（`OKCARD_PROFILE progressive texture_ms` 0.2–0.9 s）；
- 工作執行緒的 UE 端 PNG 預解碼（0.24–0.57 s）。

打包版的 `ApplyPartMaterial` 只套 cooked 材質，傳進來的貼圖都不會用到；`MakeOvertexMaterial` 等材質圖建構也只在編輯器存在。所以 **C6**（`OK_FIRSTLOAD=C6`，只在打包版且該部件有 cooked 材質時生效）做兩件事：
- 部件建構不再建貼圖。`Part.Texture` 直接取 cooked 材質綁定的 `_MainTex`；`Part.Overtex`／`Overtex2` 只在 PNG 路徑也會設的部件上，取 cooked 材質的 `_overtex1`／`_overtex2`。
- `PrecacheTextureJobs` 不解碼，log 會寫 `skipped=cooked`。

踩過的坑：第一版把每個部件的 `Part.Overtex` 都設成 cooked 的 `_overtex1`。但眼睛的 `_overtex1` 其實是眼睛層，而 `Part.Overtex` 非空時會加一個 overtex 網格段；打包版沒有它的材質，結果用預設材質蓋住眼睛，眼睛變黑。改成只在 PNG 路徑也會設的地方才設之後就正常了。

驗證：C030、C032 的 C5／C6 截圖逐像素比對，差異和兩次 C5 之間同級，最大 7–16/255，沒有超過 24 的像素。

實測（`CardReaderC4_03.cardstage`，其餘條件同上，bestC5／bestC56 交錯各 3 輪；中位數，括號內為最小值，單位秒）：

| 變體（打包版） | C030 載入＝最終 | 工作執行緒貼圖準備 | UE 端載入 | C032 載入＝最終 | C032 UE 端 |
|---|---|---|---|---|---|
| best＋C5 | 5.08 (5.01) | 0.61 | 1.55 | 9.38 (7.88) | 3.05 |
| **best＋C5＋C6** | **4.74 (4.57)** | **0.47** | **1.14** | **7.53 (7.06)** | **1.39** |

和編輯器 best 比較：

| | 編輯器 best | 打包 best＋C5＋C6 | 變化 |
|---|---|---|---|
| C030 載入 | 4.58 | 4.74 | +0.16 s |
| C030 最終 | 6.28 | 4.74 | **−1.54 s（−25%）** |
| C032 載入 | 8.89 | 7.53 | −1.36 s |
| C032 最終 | 10.68 | 7.53 | **−3.15 s（−29%）** |

打包版剩下的大頭是原生開卡（C030 3.5 s、C032 5.8–6.2 s），UE 端只剩 1.1–1.4 s。

## 部件材質參數覆寫（2026-10-18，給 OK Studio 道具花紋、汁的貼花、SkinEffects）

`AKKPreviewActor::SetMaterialOverrides(TArray<FOKMaterialOverride>, TArray<FString>* OutUnapplied)` 是通用版，以後取代 `ItemColorOverrides` 這種個別做法（`ItemColorOverrides` 暫時保留給 OK Studio 現有程式）。

- 每筆 `FOKMaterialOverride`：部件比對 `Role`、`SourceProperty`（卡片欄位，Rust `ok_part_material_metadata` 新增 `source_property`）、`Slot`（session 部件索引）、`Material`（來源材質名）；空字串／`INDEX_NONE` 表示不限。`Parameter` 是 Unity shader 屬性名。`Kind`：`Scalar`、`Vector`（照原值寫，UV 轉換、`_ST`）、`Color`（Unity Color，跟卡片一樣是 sRGB，RGB 轉線性、alpha 不動）、`Texture`（PNG 用 `OKCreateRuntimeTextureFromPng`；花紋遮罩是線性貼圖，`SRGB=false`）。
- 設定時整份清單取代舊的：先把舊清單寫過的參數還原成原值，再立即寫到每個符合部件自己的 MID。材質共用（`KKMaterialDedupe`，預設開啟）的 UMaterial 不會被改；部件槽位如果還不是 MID，就替該槽建一個。
- 重套時機：載入完成（`LoadSessionBatch` 結尾）、單欄飾品換裝（`ApplyAccessoryGeometryCandidate`）、漸進式材質（C3）全部換成最終材質時。替代材質還在顯示的槽位，覆寫直接寫在它待換上的最終材質（同 `SetClothingHidden` 的做法），所以換上時就帶著覆寫。
- 回傳寫入次數；`OutUnapplied` 列出沒有任何部件材質有該參數的覆寫（log：`OK material overrides=... unapplied=...`）。

### 各路徑支援的參數

| 路徑 | 支援 |
|---|---|
| 打包版 cooked 共用材質（`KKPreviewRuntimeMaterials.inl`、`KKRuntimeSharedMaterial.cpp`） | 每個 uniform 和貼圖本來就是同名 MID 參數，全部支援；float4 也會同步 `<名>__A`。Studio 道具（`main_item_studio` → `M_ForgeStudio`）的花紋分支本來就在 cooked 母材質裡，**不需要重新 cook**。代價：cooked 道具不論有沒有花紋都跑花紋分支（原本就如此）。 |
| 編輯器 Forge item／MainItemPlus／Forge opaque／Forge alpha／原生皮膚（`KKForgeItem.cpp` 等、`KKNativeSkin.cpp`） | 這些圖把 uniform 烤成常數。載入時已知的覆寫名稱（`MaterialOverrideNames`）才會把該 uniform／貼圖改成同名參數（`KKMaterialExposure.h`），所以要在 `LoadSession` 前設好，或看到 `OutUnapplied` 後重載 actor。沒有覆寫的部件圖完全不變。原作 `Shader Forge/main_item`、MainItemPlus 本來就沒有花紋，不加。 |
| 編輯器 `main_item_studio`（kind 4，Studio 道具一般圖的 item_draw 混色） | `_Color`.. `_Color4` 被覆寫時改成參數。任何花紋屬性被覆寫時加上 3 個花紋槽（照原作 PS SHA256 8e640300… 的運算），三槽參數全部可即時改；沒花紋的道具不多任何 shader 成本（「有花紋」是圖的一部分，材質 key 自然分開）。 |

花紋參數（原作 `main_item_studio` 屬性名，k=1..3）：`_PatternMaskk`（貼圖，預設 white）、`_PatternMaskk_ST`（Vector，預設 (1,1,0,0)）、`_Patternuvk`（Vector：xy 平移、zw 縮放，預設 (0,0,1,1)）、`_patternrotatork`（Scalar，×π 弧度，繞中心）、`_patternclampk`（Scalar 0/1）、`_Colork_2`（Color，花紋色，預設 (100,100,100)/255）；底色 `_Color`、`_Color2`、`_Color3`。每槽顏色 = lerp(`_Colork_2`, `_Colork`, saturate(max(1−mask.a, mask.r) + 1−`_Colork_2`.a))，再照 `_ColorMask` 的 RGB 混合。

自測：`ok.AuditMaterialOverrides`（`KKMaterialOverrideAudit.cpp`；`OK_MATOVR_OUTPUT` 結果 json，截圖放旁邊）。`OK_MATOVR_CARD=<卡>`：卡上有 `main_item_studio` item_draw 部件就測花紋，沒有就把最大的非頭部 Forge item／MainItemPlus 部件的 `_MainTex` 換成棋盤格（用 `Slot` 指定部件）；不設卡時找基礎庫 Studio 目錄裡第一個 kind 4 的帶花紋道具。流程：載入前設覆寫 → 漸進式材質全部換成最終材質後檢查參數 → 即時換值（不重建）→ 清空檢查還原，各截一張圖。

2026-10-18 實測（編輯器 `-game`，`Saved/APNGCards/KK_346095_apng_edited.png`，部件 43 `o_acs_head_mendako_00`）：替代材質 85 個全部換成最終材質後覆寫仍在（2 個槽位，前向＋描邊），即時換貼圖寫入 2、未套用 0，清空後還原；截圖 `Saved/Tests/MaterialOverrides/card_pattern|live|cleared.png` 分別是黑白棋盤、粉青棋盤、原本的白色。

2026-10-18 第二輪（OK Studio 重錄基礎庫 Studio 道具、帶出依賴包材質後）：預設模式選到「スフィア(通常）」（`main_item_studio_alpha`，kind 4、item_draw 3）。修了一個編譯錯誤：純量參數是 float1，DXC 不接受 `float4(float1)`，讀純量改成 `(%s).x`。結果 valid：替代材質換完後花紋和 `_Color1_2` 都在，即時改 `_Patternuv1`／`_patternrotator1`（0.25 = 45°）寫入 4、未套用 0，清空還原。截圖 `Saved/Tests/MaterialOverrides/audit_pattern|live|cleared.png`：棋盤花紋縮放 4 → 旋轉 45°、縮放 2 → 無花紋。**已解決**：當時顏色很淡（紅 `_Color1_2`、黃 `_Color` 只看到淡粉），原因不是混色也不是 alpha，而是小串流貼圖解碼錯誤：球的 `_ColorMask`（DXT1 8×8，資料在 .resS）讀完串流後又被 `repair_inline_texture_payload` 用物件檔頭裡的假長度換掉，遮罩變成 R≈0.2–0.45 的雜訊。OK Studio 修了 `unity.rs` `read_texture_payload`（串流讀到且長度等於宣告大小時不修補，測試 `sphere_mask_is_red`），`SESSION_CACHE_REVISION` 升到 4，重錄受影響的 37 個 bundle、Studio 道具與地圖。之後 `OKStudio.PatternTest` 通過，球是鮮紅條紋配白底。

（以下是第一輪的紀錄）**沒驗證到的部分**：`main_item_studio` 花紋分支（編輯器圖）建置通過，但還沒有實際畫面驗證。原因是基礎庫（OK model）的 Studio 道具開成 session 後部件**完全沒有材質資訊**（`ok_part_material_metadata` 的 `material` 是 null，shader_kind 0、沒有 item_draw；目錄前 80 個帶花紋的道具都一樣），編輯器只畫主貼圖，顏色（包括現有的 `ItemColorOverrides`）和花紋都沒有地方接。要讓 OK Studio 道具在編輯器顯示顏色和花紋，得先讓 `okmodel_view::prefab_session_fk` 帶出原本的材質（shader 名、`_Color`、`_ColorMask`）；這是基礎庫的後續工作。測試卡上的 kind 4 部件是 `main_item_studio_alpha`（不走 item_draw 圖），也用不上。

## 離線 APNG 共用動畫

讀取 APNG 後可以按「走路」，或在動作來源選 `studio/anime/00.unity3d` 再選 `f_aruki_00_00_01`。使用專案已匯入的共用原始曲線與卡片保留的骨架，不需要遊戲目錄。其它共用片段按相同 Source|Clip 身分列出；尚未匯入的 Animator 控制器狀態會明確提示缺資源，不會換成其它姿勢。性能续修與實測見 [PAINT_PERFORMANCE285.md](PAINT_PERFORMANCE285.md)。
