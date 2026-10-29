# ok-extract 重構計畫（2026-10-28）

`Tools/ok-extract/src/lib.rs` 約 18,600 行、320 個函式，`unity.rs` 約 11,000 行。開卡主流程 `extract_session_from_info` 一個函式約 2,600 行（lib.rs 約 8,400–11,000 行），哪些階段只讀資料、哪些只做運算要靠讀程式碼和量測才分得出來；轉卡加速（「只讀不組裝」）就卡在這裡。

## 原則

- **只搬不改**：每一步只移動程式碼、拆函式，不改行為。每步一個 commit。
- **每步驗收**：`cargo build --release`、`cargo test --release --lib`；6 張樣本卡（Alice、Denia、小绯雪、丽塔、雷姆、March_7th）轉換後 outfit 0 摘要與 `pack-keys` 記錄的鍵與值和重構前相同；冷讀摘要相同；`ok_cards.lua verify` 全過。
- **共用工作樹**：lib.rs、unity.rs 常有其他 session 未提交的改動（例如 okchara）。動手前先看 `git status`，與正在改這些檔案的 session 協調時段；搬移前先讓它們提交，避免搬動未提交的別人的程式碼。
- 新模組沿用周圍的命名、註解密度與寫法。

## 步驟

1. **測試移出**：lib.rs 後半的測試（約 11,000 行以後）搬到 `src/tests/` 底下的模組檔，主檔只留 `#[cfg(test)] mod ...;`。
2. **開卡主流程拆階段**：`extract_session_from_info` 依現有 `OK_PROFILE` 的階段拆成具名函式，放進 `src/extract/`：清單與 GUID（lists+mods+guids）、基礎 bundle、身體與 uncensor、頭與臉、hair/acc 解析、配件、衣服、延後網格、寫出、session 組裝。階段之間傳遞的狀態收成結構。
3. **讀取與組裝分開**：每個階段內把「讀資料」（bundle、清單、記錄查詢）和「純運算」（貼圖解碼、配色與 GPU 烘焙、合成、蒙皮）分成不同函式；轉卡的「只讀」模式改成在階段層級跳過純運算，取代目前散在各函式開頭的 `okstrict::assembling()` 檢查。
4. **lib.rs 其餘按領域分檔**：
   - `entries.rs`：`meshes_for_entry*`、預取、entry 記憶。
   - `texture_bake.rs`：PNG 工具、sRGB 表、取樣、合成、虹膜／皮膚烘焙、衣服顏色遮罩（`GridSampler`）。
   - `clothes.rs`：衣服圖樣、GPU 烘焙、native over mask。
   - `accessories.rs`：配件與頭髮配色、Studio 道具透明度。
   - `material_editor.rs`：`me_*`。
   - `body_face.rs`：皮膚目錄貼圖、頭部細節、kano 版本。
5. **unity.rs 分檔**：bundle 解析與快取、貼圖解碼（BC、payload→PNG）、網格與 prefab、材質。
6. 每步完成後更新 AGENTS.md「Rust 核心與 C ABI」的模組表。

## 順序

在「只讀不組裝」實驗量完並提交之後、全部轉換之前進行。步驟 2、3 直接服務轉卡加速；1、4、5 可以穿插在其他工作之間。

## 進度（2026-10-28）

- 1 完成（610d1396d）：測試移到 `src/tests.rs`，lib.rs 18,600 → 11,500 行。
- 2a 完成（0064a77d3）：`extract_session_from_info` 原封不動移到 `src/extract.rs`。
- 2b 完成（a61fd60a2）：「清單＋mods＋GUID」拆成 `lists_stage`，回傳 `ListsStage`；兩個 RAII scope（穿著狀態、預取）放在結構裡，呼叫端以 `_clothes_scope`、`_prefetch_guard` 綁定，保持原本的釋放順序。
- 4 lib.rs 分檔完成（d5e545985 … 7ab6363c3，一群一個 commit）：`texture_bake`、`face_texture`、`clothes_texture`（含 8c53dae5e 欄位可見性修正）、`placement`、`face_shapes`、`rig`、`entries`、`catalog_textures`、`head_body`、`material_editor`、`push`、`accessories`。lib.rs 18,600 → 976 行，剩模組宣告、Manifest 等共用型別、shader 分類、開卡對外 API 與 FFI 小工具。做法：`move_items.lua` 把連續的頂層項目搬到新檔、沒有 `pub` 的改 `pub(crate)`，lib.rs 留 `mod x; pub(crate) use x::*;`（區塊有對外 `pub` 項目時用 `pub use`），其他地方的 `crate::名稱` 不用改；結構欄位與 `thread_local!` 內的 static 若被外面用到，另外改 `pub(crate)`。
- 5 unity.rs 分檔（86f8c2454 … 26a235da7）：測試移到 `unity/tests.rs`；`textures`、`tints`、`prefabs`、`clothes_states`、`mesh_channels`、`mesh_decode`（模組放在 `src/unity/`）。unity.rs 11,000 → 5,055 行，剩開頭的型別與兩段 `impl ParsedBundle`（約 2,700 行）；拆 impl 要逐一把被別處呼叫的方法改成 `pub(crate)`，列為 5g。
- 2c 完成（2026-10-29）：「基礎 bundle」拆成 `base_bundles_stage`，回傳 `BaseStage`（25 個欄位；後面各段會改的照樣以 `mut` 綁定）。這一段只看卡片的性別、頭部、眨眼與 resolves，與服裝和穿著狀態無關，所以用 `view_memo` 包起來，鍵就是這些欄位。轉卡時同一張卡開 42 次（7 套服裝 × 6 種穿著狀態），只有第一次真正讀取並組裝。原版清單目錄（`load_characustom_dir_shared`）也一樣：基礎庫的清單資料夾不是實體資料夾，原本的變更監看快取存不住它，每次開卡都重新解析一次（約 30 ms）。jeanne 轉一張 24.8 → 17.2 秒。驗收：6 張樣本卡相同；3 張卡 × 7 套服裝 × 6 種穿著狀態共 126 個 session，開著與關掉 `OK_NO_VIEW_MEMO` 逐位元組相同。
- 2d 第一部分（2026-10-29）：
  - 轉卡時 thread 的 bundle 快取上限改成 160（原本 24，滿了就整個清空）。一張卡會碰到 60–150 個 bundle，原本每次開卡都會清空快取再全部重新 parse，bundle 上已解碼的貼圖也跟著丟掉。jeanne 的 parse 次數 933 → 69，raiden_shogun 41 → 32 秒，記憶體峰值不變。遊戲開卡仍是 24，因為快取在開卡結束後會一直留著。
  - `CardInfo.source_digest`：卡片資料（IEND 之後的位元組）與解析模式的雜湊，給要讀很多卡片欄位的階段當 memo 鍵。
  - head_detail 每個頭部網格的 rig 與貼圖，以 `(source_digest, 服裝, 網格)` 共用；有 OK Studio 眼型時照舊每次重算。jeanne 42 次開卡的 head_detail 合計 1.4 → 0.6 秒。
- 2d 第二部分（2026-10-29）：`rig_memo`（rig.rs）。身體、臉、嘴唇、生殖器的 rig／蒙皮以 `(source_digest, 服裝, 部位, 網格)` 共用，鍵要能決定 rig 前的網格與骨架。OK Studio 的狀態覆寫（`studio_status::apply`）也併進 `source_digest`。jeanne 42 次開卡的臉部準備合計 0.8 → 0.3 秒，整張卡 13.6 秒（今早 24.8 秒）。`compose_body_texture` 仍以網格內容雜湊為鍵（傳進來的身體網格可能隨穿著狀態不同），每次約 10 ms。
- 2d 第三部分（2026-10-29 下午，e-okcard 新 session）：同一張卡 42 次開卡之間的重複工作。每一項都驗收「6 張樣本卡轉換的 digest／卡包記錄／okLt 與 HEAD 相同」與「march_7th、C018、jeanne 共 126 個 session（`ok-card-open-bench --fresh-dump`，`OK_FRESH_STATES=1`）與 HEAD 逐位元組相同」。
  - 衣服／配件網格的 entry 記憶鍵（`885a4a3c7`）：穿著狀態只透過 prefab 的狀態節點（`n_top_a`…）決定留下哪些網格，所以鍵改成「這個 prefab 的狀態節點在目前狀態下的可見性」（`collect_state_nodes`／`STATE_NODES`，第一次抽取時記下節點，鍵含來源與卡包身分）。Alice 42 次開卡的 `meshes_for_entry` 457 → 128 次。
  - 身體貼圖合成（`885a4a3c7`）：鍵從「卡＋服裝」改為「卡＋KSOX 覆蓋圖內容＋身體貼圖內容」（兩段 CRC-32），沒有服裝專屬覆蓋圖的卡 7 套服裝只合成一次。
  - 衣服 create 上色（`885a4a3c7`）：`bake_clothes_create_memo` 以（檢視、卡、服裝、部件）記住，穿著狀態不影響。
  - `fallback_identity` 依後備世代算一次（每次 stat 所有後備 zip 並列出卡片資料夾，轉一張卡被問約 1000 次、每次約 1 ms），`me_shader_registration` 以 `view_memo` 記住（約 2500 次）（`23b815664`）。
  - 配件的延後網格轉換（`a8c88f7c7`）：`PushBatch::flush` 對配件項目以「網格內容＋轉換讀的輸入」的串流雜湊（`StreamHash`，CRC-32＋四路乘法混合＋長度）為鍵，放進 `view_memo`。鍵不含穿著狀態（配件只在外衣 alpha mask 時讀它，配件呼叫沒有）、服裝編號與欄位名（只用來找這個欄位、這套座標的 ME 列，而這些列本身在鍵裡）。同一頂頭髮每張卡只轉一次。
  - zipmod bundle 快取上限 256 MiB／128 筆 → 768 MiB／1024 筆（一張卡內原本清空兩次）；DXT1、BC7 與 512² 以上 DXT5 的 texel 轉 PNG 以內容跨卡共用（`png_transform_cache`）（`9797de491`）。
  - `SessionPart::from_mesh` 對 main_alpha 的「全不透明」檢查以內容記住（`8b408c1e4`）。
  - 配件欄位的上色與 push 移到延後批次的 flush（`accessory_extract::Deferred`），所有欄位的上色同時做，再依原順序 push（`0ae3ce540`）；預取結果在 helper 學到狀態節點後仍交得出去（`entry_memo_keys`，`5677d8b9d`）。march_7th 冷開 2.9 → 2.1 秒。
  - 結果（單執行緒、同機有兩個遊戲在跑，數字只能互相比）：Alice 穩態一張 22.6 → 約 16 秒；6 張樣本卡同一程序轉換 238 → 75–120 秒。一次狀態重開剩約 170 ms（延後網格 60、resolves 44）。雙 slot 的 100 張批次與冷讀見 `Docs/OK_BASE_LIBRARY_PLAN.md`「批次轉換」（491.6 → 322.3 秒；冷讀最慢 2.20 秒）。
  - 試過但不採用：`OK_RAW_TEXTURES=1`（管線內不用 PNG）在冷開反而更慢（march_7th 4.5 → 5.3 秒、C018 3.2 → 3.6 秒，同一時段交替量）。

## 做法

- 開發在自己的匯出副本（`git archive HEAD Tools/ok-extract Tools/vendor`，另外複製編譯期 `include_*!` 用到的 `Plugins/KKUnityBundle/.../Shaders`、`Plugins/OKAnimation/Resources/kk_cf_j_rest.json`、`Content/IndependentResources/BodyInteractions/hit-reaction-v1.json`、`Tools/base_library_shared_mods.txt`），用獨立的 `CARGO_TARGET_DIR`。
- 每步：release 建置、`cargo test --release --lib --no-run`；6 張樣本卡用重構前的程式做一次基準（含穿著狀態），重構後轉換比對 outfit 0 摘要與 `pack-keys` 的鍵與值（HashSet／HashMap 序列化順序不定的 `clothes_renderer_map`、`accessory_color_use`、`prefab_node_names` 只比鍵）。
- 寫回：工作樹的 lib.rs 常有別人的未提交改動；以 HEAD 版本做同樣的變換後 `git hash-object --path` 寫進暫存區，確認與匯出副本相同再提交，別人的改動留在工作樹。
