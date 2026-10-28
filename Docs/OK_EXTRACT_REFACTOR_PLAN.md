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
- 下一步 2d：身體與 uncensor、頭與臉（`head_detail`、`uncensor genitals`、`genital+mouth+face prep`，jeanne 合計每卡約 3 秒）。其中身體的透明遮罩依穿著狀態（`clothes_alpha_state`）、over body mask 依服裝，這兩項要留在 memo 外面。

## 做法

- 開發在自己的匯出副本（`git archive HEAD Tools/ok-extract Tools/vendor`，另外複製編譯期 `include_*!` 用到的 `Plugins/KKUnityBundle/.../Shaders`、`Plugins/OKAnimation/Resources/kk_cf_j_rest.json`、`Content/IndependentResources/BodyInteractions/hit-reaction-v1.json`、`Tools/base_library_shared_mods.txt`），用獨立的 `CARGO_TARGET_DIR`。
- 每步：release 建置、`cargo test --release --lib --no-run`；6 張樣本卡用重構前的程式做一次基準（含穿著狀態），重構後轉換比對 outfit 0 摘要與 `pack-keys` 的鍵與值（HashSet／HashMap 序列化順序不定的 `clothes_renderer_map`、`accessory_color_use`、`prefab_node_names` 只比鍵）。
- 寫回：工作樹的 lib.rs 常有別人的未提交改動；以 HEAD 版本做同樣的變換後 `git hash-object --path` 寫進暫存區，確認與匯出副本相同再提交，別人的改動留在工作樹。
