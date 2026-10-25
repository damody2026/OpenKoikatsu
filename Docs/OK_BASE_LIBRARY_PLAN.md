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
  卡片存的每一套服裝都會開一次錄進同一包（2026-10-24 起；以前只錄第 0 套）。只有某套服裝才用、基礎庫又沒有的資源，例如 mod 上衣給身體的 `OverBodyMask`，以前不在包裡，讀卡器換到那套就缺；這類缺口現在會印 `OverBodyMask ... not found`。舊卡用 `--oklib card`／`cards` 重轉（輸入可以是 OK 卡本身，會先剝掉 OK 區塊）。重轉已是 OK 卡的卡時，新卡帶 `okId` 區塊記下原本的身分（街區的 `apng-<id>`），名字、個人設定、存檔照常對得上（`oklib::keep_identity`、`card_identity`）。
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
