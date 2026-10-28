# OK 基礎模型庫（OK Base Library）：生成角色卡不再需要 Unity 資源

> 2026-10-15 自 OK_Demo（`E:\PhysX\UE5.8\Samples\OK_Demo`，已退役）移入本專案。路徑與參數已改為本專案的名稱：OK_Demo → OpenKoikatsu、`Scripts/*` → `Tools/*`、`-Demo*` → `-Pair*`。整合計畫見 `Docs/PHYSX_ENGINE_PORT_PLAN.md`。

## 目標

把生成角色卡用到的**共用**遊戲資源（原版 `abdata` 與角色相關 mod）**直接轉換成 OK 模型格式**，之後不再需要原本的 Unity 檔案。

- 轉換以「資源」為單位，不經過任何角色卡：每個 Unity 來源（bundle）轉成一個 `.okmodel`。
- 只收共用資源；卡片專屬資料（卡內嵌的 Material Editor 貼圖、塗裝、參數、體型）一律從卡片讀。
- 基礎庫裡沒有任何 Unity 檔案。

## 基礎庫內容（`Content/IndependentResources/base_v1/`）

| 檔案 | 內容 |
|---|---|
| `oklib.json` | 標記與版本 |
| `models/<xx>/<id>.okmodel` | 每個 Unity 來源一個 OK 模型（id = 來源長度＋CRC32） |
| `sources.okidx` | 遊戲目錄結構：路徑 → 模型、mod manifest／清單文字、設定檔、外掛是否存在 |
| `converted.tsv` | 轉換紀錄（來源、狀態、耗時、內容統計），可續跑 |
| `missing.txt` | 重播時查不到的資料（涵蓋缺口） |

### `.okmodel` 格式

`OKMODEL1` + 16 位元組來源 id，後接只追加的紀錄：`[u32 長度][u16 查詢名長][查詢名][u32 參數長][參數 bincode][u8 種類][資料]`。
種類 0：LZ4 壓縮的結果（bincode）；種類 1：指向同模型內相同結果的位移（去重）。

模型內容（依查詢名）：
- `texture_directory`：貼圖目錄（名稱、面積、別名、精確查找結果、是否完整）；`texture_png_named`、`texture_sampling_named`、`texture_color_space`、`texture_gpu_named`（GPU 用原始 texel）。
- `prefab_directory`：prefab 根、GameObject 名、網格名；`meshes_for_maindata`（含／不含停用渲染器，衣服穿脫狀態另存成節點鏈，由卡片決定）。
- `skeleton`、`skeleton_for_prefab_name`、`skeleton_full_named`、`skeleton_for_mesh*`；`skel:body_dynamics`、`skel:body_colliders`、`skel:bone_implants`（以骨架拓撲為鍵）；`dbone_sets`。
- `accessory_color_use`、`accessory_body_binding`、`clothes_renderer_map`、`prefab_node_names`、`face_runtime_settings`、`eye_material_settings`、`face_control_patterns`、`studio_pv_config`、`eye_look_config`。
- `material_named`、`renderer_material_slot`、`shader_pixel_dxbc_named`、`shader_declares_property`、`shader_bundle_has_dxbc`。
- `text_assets*`（清單）、`base_bundle`、`decode_all`、`textures_keep_base`、`quality_settings`。

依名稱模糊比對貼圖、判斷 prefab 是否存在，由目錄資料計算（Unity 與 OK 兩條路徑共用同一套程式），不需列舉參數。

## 運作方式

- `gamefs`：主流程讀遊戲檔都經過它。OK 模式下，遊戲根目錄就是基礎庫；bundle 讀到的是記憶體內的模型代號，Unity 查詢由模型回答。
- `oklib`：Unity 查詢層（`ParsedBundle` 方法與少數直接解析函式）統一走 `memo`，轉換時寫入模型，OK 模式從模型讀。
- 主流程（`extract_session_from_info`）不變，兩種模式產生的 session 逐位元組相同。

## 命令（`ok-extract <game> --oklib ...`）

| 命令 | 用途 |
|---|---|
| `convert <lib> [threads] [filter]` | 全量轉換（可續跑） |
| `replay <lib> <card>...` | 只用基礎庫產生角色（遊戲目錄可不存在），與 `parity.tsv` 比對 |
| `record <lib> <card>...` | 開卡同時補錄（除錯用） |
| `live <card>...`、`diff a b` | 一般模式 digest、兩個 session 的結構差異 |
| `models <lib>`、`sources <lib>`、`probe <lib> record\|replay` | 統計與檢查 |
| `card <lib> <KKS 卡> <out>` | 原作卡 → APNG 卡：基礎庫＋此卡 zipmod 開卡，基礎庫沒有的部分錄成卡內 `okLc` |
| `kks-verify <lib> <清單>` | 原作卡：遊戲模式 digest ＝ 基礎庫＋此卡 zipmod digest（`work/kks_verify.tsv`） |
| `okcard-verify <lib> <清單>` | 原作卡 → APNG 卡 → 只用 OK 資料重開：三者 digest 相同、大小比例、`okLc` 大小（`work/okcard_verify.tsv`） |
| `seal <lib> <OK 卡>... [--out 目錄]` | （舊卡用；新轉的卡不需要）讓既有 OK 卡只靠自己開卡：補上 `okLt`，並把它從基礎庫讀到的第三方 mod 資料複製進 `okLc`（見下方「OK 卡只靠自己開卡」）；原地改寫或寫到目錄。`card`／`cards` 轉出的新卡已經封好。批次用 `Tools/seal_ok_cards.lua`（先備份、新程序嚴格檢查通過才換掉原檔） |
| `strict-check <lib> <OK 卡>...` | 每套服裝都以嚴格模式開一次（不經快取）；一張卡一個新程序最可靠 |

## 階段與驗收

| # | 項目 | 驗收 | 狀態 |
|---|---|---|---|
| B1 | Unity 查詢層可錄製；與卡片狀態有關的部分拆出；結果確定性 | 直接擷取、錄製、讀回三者 session 位元組相同 | ✅（Alice 卡 20ad6607…） |
| B2 | `gamefs` 閘道（讀檔、列目錄、存在檢查、zip） | 一般模式行為不變；無遊戲目錄可重播 | ✅（7 張真實卡 MATCH） |
| B3 | `.okmodel` 格式、模型目錄（貼圖、prefab） | 改成模型檔後仍 MATCH；live 輸出不變 | ✅ |
| B4 | 轉換器 `convert`（直接逐資源轉換） | 31 個身體來源 132 秒、58 模型 | ✅ |
| B5 | 依需求轉換：498 張真實卡（UserData 每 19 張取 1）用 `needs` 找缺的來源、`convert @清單` 只轉那些 | 缺口清單收斂 | ✅ 4,300 個來源（原版全部＋卡片用到的 mod），約 17 GB |
| B6 | 分批驗收（每批 10 張，`verify`） | 遊戲模式與基礎庫模式 digest 相同；OK 卡 ≤ 原 PNG 110% | 🔶 前 70 張：68 張一致、2 張改流程後一致；全 498 張上一輪 483/490 一致，差異已逐一修正但尚未整輪重跑；OK 卡最大 1.0124 倍 |
| B7 | 開卡快取（`%LOCALAPPDATA%/OpenKoikatsu/oklib-cache`，4 GB 上限） | 第二次開同卡直接讀 | ✅ 程式（未在 UE 實測） |
| B8 | UE 整合：`okLb` 卡視同可攜卡，Rust 由 `OK_BASE_LIBRARY` 找基礎庫 | 遊戲內開 OK 卡、換衣服 | 🔶 已建置，未實機測試 |
| B9 | ~~全量轉換其餘 mod~~ | 不做：基礎庫只收共用資源，卡片專屬資源見「卡片的資源來源」 | ❌ 取消（2026-10-15） |
| B10 | OK 服裝 `.okclothes` | 匯入／匯出／穿上；往返 MATCH；大小 1.0000 | ✅ 指令列（UE 介面未做；ABMX 服裝資料未轉） |
| B11 | 文件與錯誤紀錄 | Plan／Errors 更新 | ✅ #77–#107 |

## 版控裡的基礎庫（2026-10-20）

版控只放共用資源：原作 `abdata` 轉出的模型、uncensor 身體、全域 mod（覆蓋原版路徑的 bundle、陰影 ramp、修正類），清單在 `Tools/base_library_shared_mods.txt`；`ok-extract <game> --oklib shared-ignore <lib>` 依 `sources.okidx` 產生 `models/.gitignore`。其他第三方 mod 的模型只留在本機，`sources.okidx` 仍記著它們：

- 有遊戲路徑時，這份 checkout 沒有的模型改讀遊戲 `mods/` 的實體 zipmod（`Fallback::installation`），基礎庫自己的模型優先；遊戲設定（`UserData/config/system.xml`、UncensorSelector 設定）也讀那個遊戲的。
- 只有卡片＋zipmod 時，UncensorSelector 設定依序找卡片資料夾、專案 `Config/UncensorSelector.cfg`。
- 轉 OK 卡時不從遊戲收共用 mod；用到的遊戲設定錄進 `okLc`，重開時不受任何一台電腦的設定影響。

詳細計畫與驗收：[BASE_LIBRARY_SHARED_RESOURCES_PLAN.md](BASE_LIBRARY_SHARED_RESOURCES_PLAN.md)。

## 卡片的資源來源（2026-10-15 決定）

基礎庫只收**共用資源**（原版全部＋常用 mod）。卡片專屬的 mod 資源不進基礎庫，只在讀取原作 KKS 卡時需要。

| 讀取的卡 | 允許使用的資源 | 不允許 |
|---|---|---|
| **我們的 APNG 卡**（OK 卡，`okLb`） | 基礎庫＋卡片本身（卡內嵌的 OK 資源包 `okLc`） | 遊戲目錄、任何 zipmod、Unity 檔 |
| **原作 KKS 卡** | 基礎庫＋此卡的 zipmod（放在卡片同目錄，或遊戲目錄 `mods/` 中卡片以 GUID 指名的 zip） | 遊戲的 `abdata`（原版一律由基礎庫提供）、其他 zipmod |

做法：

- `gamefs` 後備（只在讀 KKS 卡時開啟）：基礎庫查不到的 `mods/...` 才到實體目錄找——卡片目錄（直接放的 `.zipmod` 視同在 `mods/` 下）、
  遊戲目錄中**此卡以 GUID 指名**的 zipmod（卡片 Sideloader 解析清單＋uncensor GUID，經 `mods::find_guids` 索引）。
  已記錄的 zipmod 中沒轉換的項目從實體 zip 讀。`mods/` 以外一律只用基礎庫。
  開卡快取的 key 包含後備可提供的 zipmod（路徑、大小、時間），所以 KKS 卡也可安全快取。
- 遊戲目錄：呼叫者傳入的目錄沒有 `mods/` 時（UE 獨立卡模式傳空字串、或設定的目錄不存在）改用 `OK_GAME_ROOT`；
  UE 由 `KKGameRoot` 設定（`DefaultGame.ini` 的 `Root`）在目錄存在時自動填入。
- 卡內資源包 `okLc` v2：每筆查詢結果（未壓縮）＋覆寫層目錄觀測，整包 zstd-19；讀卡時解一次、以 Store 重建模型檔放進
  `%LOCALAPPDATA%/OpenKoikatsu/oklib-cache/packs/<hash>/`。錄製時查詢結果比照基礎庫存成精簡形式，並當場還原一次以錄下它引用的貼圖查詢。
- UE：`FOkSession::bPortable` 改由 Rust 判斷（`ok_session_has_game_root`），基礎庫建出的 session 一律走共用動作資源；
  `LogOkExtract` 會記錄每張卡的資源來源（基礎庫＋幾個 zipmod／是否用到 zipmod 資料／是否掛上卡內資源包／是否來自快取）。
- KKS 卡 → APNG 卡（`--oklib card`）：先以「基礎庫＋後備」開卡，記下由後備提供的 zipmod 來源，把它們轉成 OK 模型，
  連同所需的目錄／清單觀測打包成卡內的 `okLc`；讀 APNG 卡時掛上這包、關閉後備，只用 OK 資源。
  卡片存的每一套服裝都會開一次錄進同一包（2026-10-24 起；以前只錄第 0 套）。
- 卡內答案 `okLt`（2026-10-28，`Tools/ok-extract/src/tree_answers.rs`）：開 OK 卡時有幾個問題原本要問「整棵 mods 樹」——基礎庫記錄的約 7300 個 zipmod：合併的 ChaList 目錄、哪個 zipmod 宣告某個 GUID、StripAll 移轉、哪個 zipmod 覆蓋某個原版 bundle、UncensorSelector 的身體／生殖器清單、MaterialEditor 有沒有註冊某個 shader。每一項都要讀遍或合併全部 7300 個記錄，新程序第一張卡要多花 0.7～1.6 秒（負載高時更久），卡片本身只問其中幾十個。
  - 現在卡片自己帶這些答案：轉卡（`card`／`cards`）做完後，只用基礎庫＋卡內 `okLc` 把成品的每一套服裝開一次，記下這些問題與答案（zipmod 路徑存成相對 `mods/` 的路徑，開卡時接回當時的 mods 目錄），zstd 後存成 `okLt` 區塊（C049 為 5.3 KB）。
  - 開卡時只查卡內答案，整棵樹一個 zipmod 都不讀；卡片沒錄過的問題是缺口（見下一項），不再回頭問整棵樹。
  - 沒有 `okId` 的卡封卡時補上 `okId`，值是原本的身分，名字、存檔照常對得上。開卡快取的 key 不含 `okLt`。
  - `ok-card-open-bench --fresh-dump <lib> <目錄> <卡>...`：每套服裝都不經快取開一次，寫出 session 位元組，用來和舊版逐位元組對照。
- OK 卡只靠自己開卡（2026-10-28，`Tools/ok-extract/src/okstrict.rs`）：開 OK 卡只用卡內資料（`okLc`、`okLt`）和基礎庫的共用資源（原作資源，以及 `shared_mods.txt` 列的共用 mod：uncensor 身體、全域修正、陰影 ramp、MaterialEditor shader）。基礎庫記錄的其他第三方 mod（`sources.okidx` 記著約 7300 個 zipmod，它們的模型不進版控，見 `models/.gitignore`）一律不讀、不列、不掃：
  - `mods/` 底下的目錄列表一律只看卡內；第三方 zipmod 的清單、檔案、bundle 只從卡內 `okLc` 取；只有第三方 mod 用到的模型（`models/.gitignore` 列的）只從卡內取。
  - 任何缺的東西（卡內沒有、也不是共用資源）記成缺口，開卡失敗並列出缺什麼，不回頭掃、不現算。沒有 `okLt` 的舊卡直接報錯，訊息附上補救指令。
  - 轉卡給 zipmod 資料夾（名稱為 `mods`，例如 `D:/KoikatuSunshine/mods`）而不是遊戲目錄時，只從裡面取 zipmod，不讀遊戲的設定檔：陰影 ramp（`UserData/config/system.xml`）用基礎庫記錄的那份，UncensorSelector 設定用專案 `Config/UncensorSelector.cfg`（`oklib::zipmod_source`）。轉卡本來就不讀遊戲的 abdata（原作資源一律來自基礎庫）。
  - 轉卡（`card`／`cards`）是單次錄製（見下方「批次轉換」），基礎庫和 zipmod 都沒有的東西就轉卡失敗並列出缺什麼。
  - 基礎庫帶自己的共用清單 `shared_mods.txt`（`shared-ignore` 時從 `Tools/base_library_shared_mods.txt` 複製），打包版沒有 `Tools/` 也能判斷。
  - 封卡（`seal`）：先錄 `okLt`；再以「補齊模式」開每套服裝，嚴格開卡會缺、但基礎庫有的資料（第三方 zipmod 的清單與檔案、模型查詢結果；基礎庫本來就沒存、以前每次現算的查詢則存下現算的值）複製進卡內 `okLc`；最後每套服裝嚴格開一次。這三步（錄答案、補齊、嚴格檢查）各在一個新程序裡跑（`seal-step`）：同一程序先前開卡留下的值（entry memo、陰影 ramp、`textures_keep_base`、解析過的 bundle）會讓後一步不去讀它該讀的東西，答案沒錄到、資料沒複製，同程序的檢查卻照樣通過。`cards` 也是一張卡一個程序（`OK_CARDS_IN_PROCESS=1` 才在同一程序轉）。
  - 卡片指名、但不在基礎庫的 zipmod 裡的大 bundle（超過映射門檻、分段讀成暫存檔）以前不會記進卡包的 zip 項目，那個部件在 OK 卡裡一直是缺的；現在轉卡時整個項目照樣記錄（`Zip::note_read`）。卡片指名的 zipmod 在轉卡時掛成 `mods/<檔名>`，基礎庫若以原路徑記著同一檔案，補齊時以「同檔名、項目清單相同」對上（`library_alias`）。
  - 以前會順手記成缺口、其實是正確答案的情況不再算缺口：bundle 裡沒有那個 prefab（`memo_prefab`）、貼圖不是外部引用（`dependency_texture_model` 沒記錄）、把資料夾當檔案讀（空的 bundle 名稱組出 `abdata`）。
  - 驗收（10-28，C049，7 套服裝）：有沒有 `okLt` 的 session 逐位元組相同。新程序第一次開卡（`OK_NOCACHE`，同時有其他工作在跑）：`lists+mods+guids` 774 → 40 ms，`uncensor body` 289 → 24 ms；讀到的 zip 從 14,641 次降到 67 次（只剩卡片自己用到的）；extract_session 3.39 → 2.03 秒。只有某套服裝才用、基礎庫又沒有的資源，例如 mod 上衣給身體的 `OverBodyMask`，以前不在包裡，讀卡器換到那套就缺；這類缺口現在會印 `OverBodyMask ... not found`。舊卡用 `--oklib card`／`cards` 重轉（輸入可以是 OK 卡本身，會先剝掉 OK 區塊）。重轉已是 OK 卡的卡時，新卡帶 `okId` 區塊記下原本的身分（街區的 `apng-<id>`），名字、個人設定、存檔照常對得上（`oklib::keep_identity`、`card_identity`）。
- 批次轉換（2026-10-28，`Tools/ok_cards.lua`）：原作 KKS 卡轉成 OK 卡的固定流程；舊 OK 卡不再處理（不封、不修）。
  - 轉換（`convert`，即 `ok-extract <zipmod 資料夾> --oklib cards`）：開多個工作程序（`OK_CARDS_WORKERS`，各自的執行緒數 `OK_THREADS`＝硬體執行緒÷程序數），每個程序初始化一次（啟用基礎庫）後從標準輸入一張一張領卡（卡的成本差好幾倍，事先平分會讓多數程序閒著等最慢的那個）；卡與卡之間清掉 bundle、entry 與遮罩烘焙記憶。每次轉換的檢視身分唯一（程序 ID＋序號）：以檢視為鍵的記憶（mods 樹的 zip 清單、GUID 索引）不能拿上一張卡的樹回答下一張（見 ERROR_LOG）。同一張卡的各套服裝共用 entry 記憶，outfit 1.. 不重解析 outfit 0 已解析的配件；錄製中的卡包放記憶體，補齊資料合併後只壓縮一次（zstd 等級 `OK_PACK_LEVEL`，預設 3）。每張卡單次錄製：每套服裝開一次，轉換模式（`okstrict.rs`）下樹問題的答案錄進 `okLt`（背後的掃描在專用執行緒池跑，讀到的不算卡片資料），基礎庫有的第三方資料複製、只有 zipmod 有的照卡包錄製，一起寫進 `okLc`；兩邊都沒有的就轉換失敗並列出。不讀遊戲安裝；身分是新卡自己的。
  - 驗收（`verify`）：一張卡一個新程序、空的暫存資料夾、沒有磁碟快取，所有服裝在預設與 `CLOTHES_STATE_VARIANTS` 的穿著狀態（全 1、全 2、全 3、讀卡器預載的兩組）都要能嚴格開卡（轉換時也開這些狀態，因為其他狀態會問別的問題）；有缺口就是轉換的 bug，修轉換器、整批重跑。嚴格開卡只抓得到「讀了卻沒有」，抓不到「該讀卻沒讀」：改了轉換器後另抽幾張卡各自單獨轉當基準，和同一程序連續轉（換順序）比 outfit 0 摘要與 `pack-keys` 的記錄鍵，要完全相同。驗收不和轉換同時跑。
  - 冷讀（`cold`）：一次一張、新程序、沒有快取，量 outfit 0 開卡。要整台機器安靜的量測（冷讀、轉卡速度、CPU 時間）先同時拿下全機的 `C:\portable\OpenKoikatsu\Saved\UnrealSlots\slot1`、`slot2`（`owner.txt` 寫「量測」與預計結束時間），量完立刻放；每次量測 30 分鐘內，中間讓其他 session 輪一輪，不用傳訊請別人暫停。
  - 安裝（`install`）：全部通過才依清單順序裝成 `C000.png`…，重寫 `sources.tsv`、`selection.json`。
  - 不用磁碟快取：開卡快取、衍生貼圖、程式庫掃描預設都關（只有設 `OK_LIBRARY_CACHE` 才用）；卡包解壓後放記憶體，大型暫存也放記憶體，開完就釋放。以前的驗收在有快取的環境做，快取擋住讀取，卡包漏錄而驗收照樣通過（見 ERROR_LOG）。
  - `pack-keys <OK 卡>` 依序列出卡包記錄、overlay 項目與 okLt 答案，用來比較兩次轉換。
  - 效能（2026-10-28，Ryzen 9 9950X3D 16 核 32 執行緒，其他 session 沒有遊戲與建置時）：一個工作程序逐張轉 16 張平均 6.9 秒／張（Alice 穩態 6.0 秒：outfit 0 約 2 秒、其餘每套約 0.5–0.7 秒）；48 張用 8 個工作程序 116.6 秒（12 個 120.3 秒，實體核心已滿）。冷讀 6 張平均 1.97 秒、最慢 2.95 秒。分析工具：`OK_PROFILE=1` 印每套服裝、轉換各段與 GPU 呼叫時間。
- 驗收：
  - KKS 卡：遊戲模式 digest ＝ 基礎庫＋此卡 zipmod 的 digest（用基礎庫沒有的卡測，包含卡片專屬 mod 的卡）。
  - APNG 卡：封閉重播（無遊戲目錄、無後備）digest ＝ 遊戲模式 digest。
  - 大小：只靠基礎庫的卡維持 ≤ 原 PNG 110%；內嵌 `okLc` 的卡 ≤ 原卡＋此卡 zipmod 的總大小（`check_ok_card_size`）。

| # | 項目 | 驗收 | 狀態 |
|---|---|---|---|
| C1 | `gamefs` 後備（只服務 `mods/`）＋ KKS 卡開啟改走「基礎庫＋後備」 | 封閉重播行為不變（既有 verify 仍一致） | ✅ 樣本卡回歸 3/3 MATCH、無缺口 |
| C2 | KKS 卡驗收工具與測試（基礎庫沒有的卡） | 遊戲模式 digest ＝ 基礎庫＋zipmod digest | ✅ 樣本外 19 張全部 MATCH：9 張 mod 已在基礎庫、10 張用到基礎庫沒有的 zipmod（`kks-select` 挑出，多為角色專屬 mod）；每張約 30–60 秒 |
| C3 | APNG 卡內嵌卡片專屬 OK 資源（`okLc`） | 封閉重播 digest ＝ 遊戲模式；列出大小比例 | ✅ 10/10 三方 digest 一致（遊戲模式＝轉換時＝只用 OK 資料重開），缺口 0；資源包 316 B–10.5 MB；APNG 卡為原卡 1.0001–1.3154 倍、為「原卡＋其 zipmod」的 10–54% |
| C4 | UE：APNG 卡封閉開啟、KKS 卡以基礎庫＋後備開啟 | 遊戲內兩種卡都能開、換衣服 | 🔶 遊戲內（RunControl）兩種卡都開啟並播放配對姿勢：KKS 卡＝基礎庫＋5 個 zipmod（讀到 zipmod 資料）；APNG 卡＝只用 OK 資料（掛上資源包、未讀 zipmod）。換衣服未測 |
| C5 | 文件與錯誤紀錄 | Plan／Errors 更新 | ✅ #113–#117 |

## OK 服裝（`.okclothes`）

一套服裝（衣服＋飾品＋該服裝的外掛資料）存成 `.okclothes`：縮圖 PNG（原服裝卡圖或角色卡圖，不重新編碼）＋一個 `okCl` 區塊
（msgpack：名稱、來源、ChaFileCoordinate 位元組、服裝層級外掛資料、用到的模型、未套用的外掛）。共用模型在基礎庫，不重複存；
大小與原服裝卡相同（實測比例 1.0000，上限 110%）。

| 命令 | 用途 |
|---|---|
| `clothes-import <服裝卡> <out.okclothes>` | 匯入遊戲服裝卡（`【KoiKatuClothes】`） |
| `clothes-export <角色卡> <套> <out.okclothes>` | 匯出角色卡的某套服裝 |
| `clothes-wear <角色卡> <okclothes> <套> <out 卡>` | 穿到角色卡的某套服裝 |
| `clothes-test <work> <角色卡> <套>` | 匯出→穿回→session 必須相同 |

外掛的服裝層級 ⇄ 角色層級轉換（與遊戲載入服裝時相同）：Sideloader（`outfitN.` 前綴）、Material Editor（`CoordinateIndex`、衣服／飾品物件、
貼圖字典重新編號）、KCOX 覆蓋、Pushup、髮飾顏色、Accessory_States（`{套: 資料}`）、moreAccessories（`accessorySet type=N`）、
DynamicBoneEditor／ASS（清單項目的服裝欄位，服裝卡分別為 0／-1）、MigrationHelper（聯集）。
尚未轉換：ABMX 服裝資料、Additional_Card_Info（保留在檔案內、穿上時回報）。

驗收：Alice 第 0／1／4 套匯出再穿回 MATCH；兩張服裝卡匯入比例 1.0000；服裝卡穿到原版角色可正常產生。

不在範圍：卡片專屬資料、動畫（已有 OK 動畫庫）、語音、地圖。

## 服裝遮罩全卡掃描（2026-10-25）

`ok-extract <遊戲> --mask-report <卡或資料夾>...`：列出每張卡每套服裝要的遮罩（OverBodyMask、OverBraMask、OverInnerMask）在哪裡找到。`ok` 讀得到、`ours` 存在但我們沒去那裡找（要修）、`mod` 任何來源都沒有（mod 本身缺，遊戲也讀不到）、`none` 該物件不要遮罩。

- 2026-10-25 掃 E 的 StreetHeroines＋CrowdAPNG 共 115 張：ok 1006、none 990、mod 38（14 個 mod，例如清單指向的貼圖名不在 bundle 裡）、ours 6。
- ours 6 都是 C032：遮罩 bundle（`chara/nashi/ns_sweater.unity3d`）由另一個 zipmod 提供；直接讀遊戲時只找物件自己的 zipmod 與 abdata。`load_over_clothes_mask` 改成和 Sideloader 一樣也找 Sideloader png 與每個 zipmod，修後 C032 全部讀到。C032 的 OK 卡轉檔時已經讀到（轉檔經 gamefs 的 mod 對應），卡不用重轉。
