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
