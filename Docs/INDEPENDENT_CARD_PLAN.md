> 現行進度：KK_346095 已拆成程式內建共用資源包與只含角色獨有資源的 PNG，見 [BUILTIN_RESOURCES.md](BUILTIN_RESOURCES.md)。2026-10-02 已新增獨立卡轉 Unreal 原生資產及九套服裝 Windows Cook，見 [INDEPENDENT_ASSET_IMPORT358.md](INDEPENDENT_ASSET_IMPORT358.md)；後續完整 mip 保存修正與Stage359 mip 資產見 [INDEPENDENT_ASSET_MIPS359.md](INDEPENDENT_ASSET_MIPS359.md)。完整骨架修正見 [INDEPENDENT_ASSET_SKIN360.md](INDEPENDENT_ASSET_SKIN360.md)；最新 Stage361AllR1 及 morph 小位移保存見 [INDEPENDENT_ASSET_MORPH361.md](INDEPENDENT_ASSET_MORPH361.md)。公開穩定 schema、全遊戲資源全集與任意原始 KKS 卡免遊戲還原仍屬後續階段。

# 無遊戲安裝的角色卡與 Unreal 資源包：執行計畫

狀態：已開始實作獨立資料夾、同目錄 zipmod 索引、身材另存與本機可攜式 3D 原型；正式共用資源包與公開 PNG v1 仍待完成。操作與驗證見 `Docs/INDEPENDENT_CARD_PROTOTYPE.md`。

2026-10-01 使用者指定第一個原型先從 `C:\portable\KoikatuSunshine\UserData\chara\female\KK_398530.png` 與現有遊戲資源轉換。這次允許轉換階段使用遊戲；轉換產物的載入、身材調整及另存必須走空白遊戲路徑。這是對本機原型的階段性調整，尚未達成「轉換前也完全不需遊戲」的最終目標。

同日使用者改指定 `KK_346095`；目前驗收入口改為 `C:\portable\KoikatuSunshine\UserData\chara\female\KK_346095.png`，先前卡的紀錄保留供比較。

## 目標與邊界

使用者只持有舊版人物 PNG、OpenKoikatsu 與本專案提供的獨立資源包時，能匯入卡片、調整身材、看到 3D 預覽，並另存為新的 PNG。卡片需要額外 mod 時，使用者把所需 `.zipmod` 放在**該卡同一目錄**即可供程式讀取。**匯入、轉換、預覽、儲存都不得要求安裝 KoikatuSunshine、`abdata` 或全域 mods 目錄。**

舊卡只提供參數與資源引用，不含完整 3D 模型。獨立資源包必須採用自行製作或已取得可用授權的模型、貼圖、動畫等內容。第一版追求可編輯且外觀一致的**替代呈現**；不承諾任意舊卡與原作逐像素一致。未支援的髮型、服裝、配件與 mod 必須列出，不能悄悄宣稱還原成功。

第一版交付範圍：Windows x64／UE 5.8、女性基礎身體與頭部、身體 44 項卡片參數、同目錄 zipmod 發現與已支援資源的轉換、十張固定驗收卡、預覽與另存。臉部 52 項、任意第三方 zipmod 功能、完整動畫與物理、跨平台封裝列為後續階段。若十卡含不在此範圍的資源，記為「可匯入但使用替代資源」，不算原作外觀還原。

## 現況與設計原則

- `Plugins/KKCard/` 已能解析舊卡的 PNG／MessagePack；Gallery 有身材滑桿，寫卡程式會更新 `shapeValueBody`。但 Gallery 從 `GameRoot/UserData/chara/` 列卡，README 尚未把「另存」驗收為完整捏人保存。
- 目前 Rust Session 從遊戲及 mods 建立 3D；快取 blob 仍含 `game_root`，載回時也會讀取遊戲資料。它是內部快取，**不可直接作為新 PNG 的公開格式**。目前 mod 索引固定從 `GameRoot/mods` 掃描；獨立模式須從卡片父目錄建立另一個解析入口。
- 目前 UE 按卡建立 transient `USkeletalMesh`、解碼 PNG 貼圖並建立材質。先前十卡量測顯示冷載入主要耗在資源抽取；即使 Rust Session 命中快取，UE 貼圖與材質建立仍有明顯時間。因此新格式與 UE 資產路徑都要改，僅把舊快取塞進 PNG 不足以達標。
- 共用資源在製作時匯入 Unreal 並 cook；新 PNG 保存可攜帶的角色參數與穩定資源 ID。不要把平台／引擎版本專屬的 `.uasset` 當成跨版本卡片格式。
- 原遊戲路徑保留為既有相容模式；獨立模式的每一層都須有不觸及 `GameRoot` 的入口。

## 交付架構

```text
舊 PNG ──卡片解析──> 中立 CharacterSpec ──資源對應──> 新 PNG v1
   │                       │                    │
   └─同目錄 *.zipmod ──GUID／檔案雜湊────────────┤
                           │                    ▼
                           │            Unreal 獨立資源包
                           │            共用骨架／材質／網格
                           └──zipmod 可支援部件的本機轉換快取
                                                │
                                                ▼
                                        UE 3D 預覽與編輯
```

**CharacterSpec** 是與遊戲及 Unreal 無關的資料契約：原始卡種類與版本、名稱、身體與臉部參數、服裝槽、來源資源引用、所需 zipmod GUID／內容雜湊、已選替代資源、缺件清單。原始來源 ID 與選定的替代 ID 分開保存，避免往返儲存時失去來源資訊。

**獨立資源包** 含共用骨架、身體／頭部／可選部件的 Skeletal Mesh、變形資料、材質母版與實例、貼圖、必要的預設姿勢及簡單動畫。使用穩定的邏輯 ID 與內容雜湊建立 manifest；執行時用 ID 找 Unreal soft object path。Editor 內的來源資產與打包後的 cooked chunk 是建置產物；兩者均須標記資產來源與授權。資源包版本與卡片格式版本分開管理。

**新 PNG v1** 保留可由一般圖片檢視器顯示的預覽圖。第一候選是 PNG 規範容許的私有 ancillary chunks：前段放小型 manifest，後段按資源分塊；讀取器只掃 chunk 標頭並 seek 到目前需要的資料。manifest 含 magic、格式版本、最小讀取器版本、角色參數、資源 ID／雜湊、缺件狀態及各塊大小／校驗值。純 PNG 圖像或私有資料被圖片服務移除時，程式必須能判別並給出錯誤；不能把圖片仍可顯示誤認為卡片資料完整。格式凍結前比較此方案與現有「IEND 後接資料」對本機圖片軟體、分享路徑、讀取速度及大檔限制的實測結果。

卡片專屬資源只有在授權及體積允許時才內嵌；共用網格與貼圖以 ID 引用資源包。需要完整單檔攜帶的角色可在後續加「自包含匯出」模式，但不是第一版的必要條件。任何嵌入資料均設解壓大小、區塊數量、尺寸與時間上限，並在使用前驗證雜湊。

### 同目錄 zipmod 契約

例如 `Cards/A.png` 只搜尋 `Cards/*.zipmod`，不遞迴搜尋、不要求 `mods/` 或其他全域路徑。讀卡時先解析卡內的 UniversalAutoResolver／相關 mod 引用，再讀取同目錄 zipmod 的 `manifest.xml` 建立 GUID 索引；只開啟這張卡實際需要的封包和條目。以 GUID 與來源欄位匹配，**不以檔名猜測**。若同一 GUID 對應多個不同檔案，顯示衝突並要求使用者處理，不任意選第一個；不存在、版本不符、依賴另一個未提供的 zipmod，也明確列出。重新儲存卡片到其他目錄時提示它需要的 zipmod 必須一起移動，或由使用者選擇一併複製。

獨立資源包提供身體、共同骨架、材質母版及必要預設；zipmod 只提供它確實包含的額外資源。轉換器重用已驗證的 ZIP／UnityFS 解析能力，把**可支援的**網格、貼圖、材質參數與綁定資訊對接到獨立骨架。若 zipmod 依賴原遊戲 bundle、原遊戲 shader／腳本或其他未提供資產，而獨立包沒有對等資料，就標記「有 zipmod，但無法完整轉換」，不能當作缺少遊戲時自然可用。

共用資源可預先 cook 成 Unreal 資產；使用者臨時放入的 zipmod 不能假設在一般打包程式中即時生成可掛載的 cooked `.uasset`。第一版在本機解析一次，產生按 zipmod SHA-256、轉換器版本、獨立包版本及平台鍵控的衍生快取，再由現有 runtime mesh／texture 建立能力呈現支援部件。相同 zipmod 在多張卡之間重用快取。若未來要把 mod 也變成 cooked Unreal 包，另外提供需要 Editor／cook 工具的可選預建流程；它不是「放同目錄就能讀」的前提。

## 里程碑與驗收

### M0：固定驗收資料及資產來源

- [ ] 選定十張女性、可合法用於本地測試的舊 PNG，優先沿用先前 profile 的卡；若其中含男性，移入後續男性驗收並補足女性樣本。至少三張要依賴同目錄 zipmod，其中一張要有兩個以上 mod。記錄卡種類、44 個身體值、服裝槽、所需 GUID、zipmod 檔案雜湊及卡片預覽圖雜湊。測試卡及 zipmod 不提交到倉庫。
- [ ] 把十卡分成「僅共用資源」「同目錄 zipmod 可轉換」「有 zipmod 但引用未支援資源／依賴」「缺少 zipmod」四組；第一版不以後兩組的外觀相似度評分。
- [ ] 確定獨立資源包的模型、貼圖、動畫來源與可用授權；記錄來源、作者、版本及允許的分發方式。沒有合適來源時，先製作最小原創測試資產，不從原遊戲或 mod 匯出後改名充當獨立包。
- [ ] 記錄現有十卡的冷／熱載入基線：從點擊至首次可見 3D、至完整可互動、各階段耗時與記憶體峰值。測試環境和快取狀態須可重現。

**通過條件：** 十卡清單、授權紀錄及基線報告齊備；確認至少一套可供實作與測試的獨立身體、頭部、骨架和材質。

### M1：無遊戲的卡片編輯閉環

- [x] 讓 Gallery 從使用者選定的卡片資料夾列卡，不再把列卡綁在 `GameRoot/UserData/chara/`。
- [ ] 選卡後只掃描該卡父目錄的 `*.zipmod`，依 manifest GUID 對卡內引用；顯示已找到、缺少、衝突和無法轉換四種狀態。即使沒有 zipmod，仍可編輯與儲存身材數值。
- [x] 將卡片解析、身體 44 項修改、另存 PNG 串成不需要 `GameRoot` 的流程；儲存目的地由使用者選定，預設另存且不覆寫來源。
- [ ] 為每種支援的舊卡布局做 read → 修改一項身體值 → write → read 的往返測試；未修改欄位、未知區塊與原始資源引用保持不變。明確記錄目前尚不能安全寫回的卡種類。
- [x] 更新 UI：沒有 3D 資源時，滑桿可編輯數值，但預覽圖仍是原圖，明示 PNG 預覽不會隨身材變更。

**通過條件：** 在完全沒有遊戲與全域 mods 目錄的測試環境，十卡都能讀取；同目錄 zipmod 能按 GUID 正確匹配，缺少或重複時有明確狀態；支援的卡可修改身體值、另存、重新讀回，值在儲存精度內一致；原檔未被修改。此階段尚不宣稱 mod 的 3D 可用。

### M2：獨立 Unreal 資源包最小原型

- [ ] 在資產製作工具中建立一套共同骨架、女性中立身體、頭部、基本材質與貼圖；固定座標系、骨名、比例、UV／材質槽規約。
- [ ] 透過 Unreal 匯入流程建立 `USkeleton`、`USkeletalMesh`、`UTexture2D` 和材質資產；材質使用預先製作的母版與實例，角色切換只改參數。
- [ ] 為身高、胸部尺寸、腰寬等至少三個代表性身材值實作獨立變形定義。區分骨骼局部變換與 mesh morph；不能把原作的 shape key 計算結果直接套到不同拓撲／骨架上。
- [ ] 建立 manifest 的 ID → 資產 soft path、版本及雜湊對應；用 Unreal cook 封裝為獨立 chunk，測試啟動時載入。

**通過條件：** 沒有遊戲目錄時，使用一張測試卡可顯示 3D；三項身材值在 0／0.5／1 三個位置有連續、可預期且無破面的變化；重新載入同一資源不重建網格或重新編譯材質。記錄 cooked 包大小與載入時間。

### M3：完整身體參數與資源對應

- [ ] 為現有身體 44 項逐一建立「卡片索引 → 獨立資產變形通道／計算規則」表；逐項標記已支援、近似或不可支援。
- [ ] 對會影響衣服、配件位置與碰撞的身體變形建立共同規則。至少用中立姿勢、轉身姿勢及一個簡單動作檢查交界、穿模和附件漂移。
- [ ] 加入基礎髮型、上衣／下衣及顏色變體；使用資源 ID 對應舊卡引用。未對應 ID 走明確的替代／缺件流程，不能依碰巧相同的數字選錯資產。
- [ ] 建立 zipmod 轉換範圍表：先驗證一個髮型、一件衣服、一個貼圖／材質覆寫，記錄每項需要的 bundle、骨架、shader 及其他 GUID；超出範圍者保留來源引用並顯示原因。
- [ ] 實作同目錄 zipmod 首次轉換與本機衍生快取；修改 zipmod 任一位元組、轉換規則或獨立資源包版本後快取必須失效。轉換失敗不得污染已可用的共用資源。
- [ ] 把臉部 52 項列為後續能力；第一版先固定中立臉或有限的明確對應，並在 UI 標示不支援項目。若十卡辨識度需要臉部變形，先把該子集納入本里程碑。

**通過條件：** 十卡都能進入獨立 3D 預覽；身體 44 項均有經測試的獨立變形對應、可寫入並重載，且在 0／0.5／1 有可見且穩定的結果；指定的髮型／衣服／貼圖 zipmod 實例在無遊戲環境成功轉換並可快取重開。每張卡的原始引用、選用資產與缺件／未支援狀態可檢視。畫面以新資源包的設計標準評估，不用原作像素差作合格線。

### M4：新 PNG v1 與舊卡轉換

- [ ] 完成格式規格：chunk 排列、編碼、版本升級、必需／可選欄位、整數溢位防護、CRC／內容雜湊、資源大小上限、未知欄位保留規則與錯誤代碼。
- [ ] 編寫獨立於 UE 的讀寫器與 golden fixtures；測試舊卡 → CharacterSpec → 新 PNG → CharacterSpec，及新 PNG 在不同軟體複製、轉存後的狀態。
- [ ] 在新 PNG 中同時保留來源卡資訊與獨立資源對應；按當前服裝分段，讀卡列表時只讀小型 manifest，選卡後才讀必要區塊。
- [ ] 在 manifest 記錄每個外部 zipmod 的 GUID、來源資源 ID、內容雜湊及是否必需；卡片與 zipmod 同目錄移動後仍可解析。另存到新目錄時不能默默變成「mod 已找到」但實際無法載入。
- [ ] 定義資源包缺失、版本不符、卡片被圖片服務剝除擴充資料、檔案損毀時的處理。列表仍顯示預覽圖；3D 開啟需給出可處理的明確錯誤。

**通過條件：** 十卡均可在無遊戲環境轉為 v1 並再次開啟；往返身材數值一致，預覽圖可由普通 PNG 檢視器開啟；損毀與缺件測試不造成崩潰或錯用其他資產。規格及 golden fixtures 版本化。

### M5：載入速度、打包與發佈驗收

- [ ] `IndependentPack` 載入路徑讀 manifest、非同步載入共用資產、套用身材及少量材質參數；有同目錄 zipmod 時先查衍生快取，未命中才解析和轉換。共用網格／貼圖重用。保留原 `GameRoot` 模式，兩條路徑在 UI 中清楚區分。
- [ ] 在未安裝遊戲且不存在舊 cache 的乾淨 Windows 測試環境跑十卡；監看檔案存取，確認獨立模式只讀卡片目錄的 zipmod 與獨立資源包，沒有對遊戲 `abdata`、全域 mods、硬編碼遊戲路徑或原始 Session cache 的成功依賴。
- [ ] 使用 packaged build 與實際 RHI 測量十卡，分「無 mod」「zipmod 首次轉換」「zipmod 衍生快取命中」三組；每組冷開（新程序、清除本程式資產快取）與熱開（同程序再次開同卡）各至少三輪。記錄首次可見、完整互動、zipmod 解析／轉換、峰值記憶體、卡片／zipmod／資源包／快取大小；註明 OS 檔案快取未必能完全控制。
- [ ] 提議效能門檻：無 mod 或 zipmod 快取命中時，十卡冷開首次可見 3D 的中位數 ≤ 5 秒、完整可互動的中位數 ≤ 10 秒；熱開首次可見的中位數 ≤ 2 秒。zipmod **首次轉換另列耗時**，不得混入快取命中成績，也不得宣稱首次載入同樣快速。這些是待原型驗證的產品目標；若失敗須以 profile 定位讀包、轉換、UE 資產建立、材質或貼圖上傳。
- [ ] 用完整打包程式完成不裝遊戲的匯入 → 改身材 → 預覽 → 另存 → 重開流程；更新安裝說明、資源清單、已支援範圍與限制。

**通過條件：** 十卡完整流程在乾淨環境成功；至少三張同目錄 zipmod 卡能以已支援資源載入，缺件／未支援部件有明確結果；沒有遊戲檔案依賴；效能報告列出每張卡及首次轉換／快取命中／熱開分布。若效能門檻未達，先完成定位與改善，再宣告「快速載入」完成。

## 驗證工作與建置入口

每個里程碑新增專屬的無遊戲測試，並在 CI／乾淨機器上運行：卡片往返與未知欄位保留、44 項身體邊界值、manifest／損毀輸入、資源 ID 解析、zipmod GUID 重複／缺失／依賴／損毀／路徑穿越、zipmod 更新後快取失效、缺件狀態，以及新舊載入路徑互不干擾。測試不得讀取本機開發者的 `C:/portable/KoikatuSunshine`。十卡檔案、zipmod 與美術來源可放在權限適當的外部測試資料目錄，倉庫只保存匿名化的預期數值與測試規則。

實作後沿用專案既有 Rust 與 Unreal 建置入口：`cargo test --release --manifest-path .\Tools\ok-extract\Cargo.toml --lib`、UE 5.8 的 `Build.bat OpenKoikatsuEditor Win64 Development`，再執行新設的 `IndependentCard` 專項 UE Automation 測試。此處的 `IndependentCard` 已建立四項原型驗收測試；M1–M5 的十卡、公開格式與 packaged build 驗收仍待補齊；真實 RHI 截圖與 packaged build 測試不能被 `-NullRHI` 測試取代。

## 先做的垂直切片

首個開發迭代先選一張無複雜 mod 的女性卡，完成 M1 與 M2 的交集：獨立列卡 → 身高／胸部／腰寬三滑桿 → Unreal 3D → 另存 → 重開。緊接著用一張帶**同目錄、可轉換髮型 zipmod** 的女性卡驗證 GUID 解析、首次轉換、快取命中與缺件提示。兩張卡都通過後才擴至 44 項、十卡與正式 v1 格式。完成此切片前，不大量製作髮衣資產，也不把目前的 `SessionBlob` 包進卡片。

## 主要風險與決策點

| 風險 | 處理方式／停止條件 |
|---|---|
| 找不到可分發的獨立資產 | 用原創最小測試資產完成原型；正式資源包來源未確定前不宣告可發佈。 |
| 原作滑桿語義與新骨架不相容 | 為新資產製作獨立變形定義，保留原卡數值但標記近似；不要複製原作骨架係數。 |
| 同目錄 zipmod 仍依賴遊戲 bundle／shader／其他 mod | 對每個資源建立依賴閉包；無獨立替代時報告未支援並退回明確替代外觀，不宣稱完整讀取。 |
| 大量未知 mods | 顯示缺件、未支援原因與替代品，不把十卡成功外推為任意卡支援。 |
| 使用者臨時加入的 zipmod 無法直接變成 cooked `.uasset` | 第一次解析轉換為本機 runtime 衍生快取；共用部件仍使用預先 cooked 資源。 |
| PNG 擴充資料在分享平台流失 | 以格式檢查及雜湊偵測；保留原檔傳輸建議，後續視測試結果提供可靠匯出容器。 |
| cooked 包與引擎／平台綁定 | 卡片只記穩定 ID；各平台、引擎版本重建資源包，manifest 明示相容版本。 |
| 快速格式仍遇到 UE 建立成本 | 優先使用 cooked 共用資產與預建材質，再 profile 首幀與互動就緒兩個指標。 |
| 同工作區有進行中的原作還原工作 | 實作時以獨立模組／分支與測試隔離；不改寫既有 GameRoot 路徑或其驗收結果。 |

## 參考入口

- 專案現況：`README.md`、`Docs/CARD_FORMAT.md`、`Docs/ASSET_PIPELINE.md`
- 卡片讀寫：`Plugins/KKCard/Source/KKCard/Private/KKCardReader.cpp`、`KKCardWriter.cpp`
- 當前 UI：`Source/OpenKoikatsu/OKGalleryWidget.cpp`
- 身材計算：`Tools/ok-extract/src/shape.rs`、`session.rs`
- 當前 UE 組裝：`Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKSkeletalBuilder.cpp`、`KKPreviewActor.cpp`
- Unreal 官方：[FBX Skeletal Mesh Pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-skeletal-mesh-pipeline-in-unreal-engine)、[Cooking and Chunking](https://dev.epicgames.com/documentation/en-us/unreal-engine/cooking-content-and-creating-chunks-in-unreal-engine)
- PNG 官方：[PNG Specification, Third Edition](https://www.w3.org/TR/png-3/)

Stage362 保存資產的骨架材質 usage、CPU 蒙皮、morph 開關及根骨回復驗證通過；見 [INDEPENDENT_ASSET_RUNTIME362.md](INDEPENDENT_ASSET_RUNTIME362.md)。完整動態 Actor/controller 與 GPU 畫面仍待驗收。

Stage363 非根骨旋轉與保存權重公式驗證通過，見 [INDEPENDENT_ASSET_JOINT363.md](INDEPENDENT_ASSET_JOINT363.md)；完整controller/GPU仍待驗收。

Stage364 已提供 Editor 專用匯入介面，操作見 [CARD_IMPORTER_UI364.md](CARD_IMPORTER_UI364.md)；原始未轉換KKS卡仍需來源資源。

Stage365 已接上專案內原始卡資源索引包及同目錄 zipmod。原版卡與模組卡不需指定遊戲本體即可建立保存資產，操作與實測範圍見 [CARD_IMPORTER_NATIVE365.md](CARD_IMPORTER_NATIVE365.md)；不能外推為任意卡／模組全數支援。
