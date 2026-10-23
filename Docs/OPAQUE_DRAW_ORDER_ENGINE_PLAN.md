# 不透明重合層照 Unity 繪製順序：引擎修改計畫

日期：2026-10-23。起因是 CrowdAPNG `C001.png` 的外套袖子在讀卡器裡閃爍（見 [ERROR_LOG](ERROR_LOG.md) 同日紀錄）。
目前的修正是 Rust 端 `separate_coincident_layers`：同一網格的 opaque／masked 層依繪製順序沿法線推 0.05 mm。
本計畫改成和 Unity 相同的機制：幾何不動，由引擎依指定順序繪製，深度相等時後畫的層蓋在上面。

## 1. 現況（UE5.8 原始碼，`D:\PhysX\UE5.8`）

| 位置 | 行為 |
|---|---|
| `Renderer/Private/BasePassRendering.cpp` `CalculateBasePassMeshStaticSortKey` | 不透明 base pass 的排序鍵只有 `Masked`、`Background`、PixelShaderHash、VertexShaderHash，沒有任何「優先度」 |
| `Renderer/Public/MeshPassProcessor.h` `FCompareFMeshDrawCommands` | 排序鍵相同時再比 `StateBucketId`，仍相同就回傳 false；`TArray::Sort` 不穩定，平行產生的命令順序每幀可能不同 |
| `Renderer/Private/MeshDrawCommands.cpp` `UpdateMeshSortKeys` | 只有關閉 early-Z（`DDM_None`）或 mobile 時，才會每幀改寫成依距離／狀態排序的鍵；桌機 deferred 預設 `DDM_AllOpaque` 不走這裡 |
| `Renderer/Private/CompositionLighting/PostProcessMeshDecals.cpp:354` | Epic 自己已經把 `TranslucencySortPriority` 放進 `SortKey.BasePass.Masked`（15 bit），作為 mesh decal 的順序：這是可以照做的前例 |

三層袖子用同一個共用 parent（ForgeItem），shader hash 相同，排序鍵完全一樣，所以每幀先後不固定。
有完整深度預先處理時，base pass 用 DepthEqual，三層都通過，GBuffer 留下最後畫的那層。
因此只要排序固定、而且順序正確，結果就和 Unity 的 LEqual + 依序繪製相同。

## 2. 引擎 patch（`UE5.8-physX-5.11-ok`，D: 與 E: 兩份引擎都要）

只改 Renderer 模組的私有 .cpp，不動公開標頭，所以只需重建 Renderer（以及連結它的 target），不會牽動整個引擎。

1. 新增 CVar `r.OK.OpaqueSortPriority`（預設 0：引擎行為完全不變）。
2. `CalculateBasePassMeshStaticSortKey` 多傳入 `PrimitiveSceneProxy`。CVar 為 1 時：
   `Masked = ((Clamp(TranslucencySortPriority, -8192, 8191) + 8192) << 1) | MaskedBit`。
   - 優先度放在最高位：先依優先度、再依 masked，最後依 shader 狀態排序。
   - 沒設優先度的 primitive 全部是同一值（8192），彼此的順序和原本一樣。
3. `MeshDrawCommands.cpp` 第 376、434 行的 `Masked == 1` 改成 `(Masked & 1) == 1`，避免無 early-Z 模式誤判。
   無 early-Z 模式的鍵沒有空位放優先度，所以在該模式下不保證順序。計畫中會在專案啟動時記錄 `r.EarlyZPass`，並在 log 中檢查。
4. `FCompareFMeshDrawCommands` 在完全相同時改成依 `DrawPrimitiveId`（或 `PrimitiveIdInfo`）比較，讓沒設優先度的重合面也每幀固定。
   這一步會改到公開標頭，屬於可選步驟；做之前要先評估重建範圍。
5. 加上供 `Build.cs` 檢查的標記字串，例如 `OKOpaqueSortPriority`。
   在 `KKUnityBundle.Build.cs`／`OpenKoikatsu.Build.cs` 加 `Patched("OK_OPAQUE_SORT_PRIORITY", "Source/Runtime/Renderer/Private/BasePassRendering.cpp", "OKOpaqueSortPriority")`。
6. 在 `Tools/EnginePatches/` 新增 `OpaqueSortPriority.patch` 和說明，README 補一列；以 commit 形式推到 `ok` remote。

## 3. 專案端修改

1. **Rust**：`separate_coincident_layers` 改成只算層序、不推幾何。
   - 在 `OkMeshView` 增加 `draw_layer`（同一網格群組內依 session 順序 0、1、2…；沒有重合者為 0）。
   - 層序在開卡時計算，存在 session 內但不序列化（`#[serde(skip)]`），以免改到 okPv 卡內快照的格式。
   - 移除 `SessionPart::push_surface` 對 `AccessoryRuntime` 來源網格的修改。
   - `SESSION_CACHE_REVISION` 改為 29，舊的推過幾何的快取不再使用。
   - `include/ok_extract.h` 與 `KKRustExtract` 同步更新。
2. **UE 讀卡器／街頭**（`AKKPreviewActor`，每個部件是獨立的 `UKKRuntimePoseableMeshComponent`）：
   - 對 opaque／masked 部件設定 `TranslucencySortPriority = 基準 + draw_layer`。
   - 現有的眼睛（101–107）、前髮（110）已占用這個欄位；不透明層使用另一段區間，並確認眼睛和頭髮的半透明排序不受影響。
   - 執行時呼叫 `MarkRenderStateDirty`，讓快取的 mesh draw command 重新產生。
3. **專案設定**：`Config/DefaultEngine.ini` 的 `[SystemSettings]` 加入 `r.OK.OpaqueSortPriority=1`。
4. **其他渲染路徑**（第二階段）：
   - 人群 GPU 打包（`KKCrowdGpuPacking`）會合併 section；同一個 primitive 內的 section 沒有 primitive 優先度可用，要確認合併後的 section 是否依 `draw_layer` 排列，或保留不合併。
   - 已匯入資產（`KKImportedAssets`）的部件元件也要帶入同一層序。
5. **未套 patch 的引擎**（`OK_ALLOW_UNPATCHED_ENGINE=1`）：原計畫保留 0.05 mm 幾何推移當退路；實作時決定不保留，未套 patch 時重合層會恢復閃爍。理由是 `Build.cs` 已會擋下沒套 patch 的建置，維持兩套機制只會增加分歧。

### 實作紀錄（2026-10-23）

- 引擎：`BasePassRendering.cpp`（CVar 與排序鍵，CVar 變更時重建 render state）、`MeshDrawCommands.cpp`（`Masked & 1`）。第 2 節的步驟 4（完全相同時的穩定比較）暫不做。
- Rust：`assign_draw_layers`（`lib.rs`）在開卡、從快取或快照還原（`into_session_with_settings`）、以及替換飾品部件時重算；`SessionPart::draw_layer` 不存入快照。`OkMeshView::draw_layer` 位於結構尾端。`SESSION_CACHE_REVISION` 改為 29。
- UE：`AKKPreviewActor` 對 `draw_layer > 0` 的部件呼叫 `SetTranslucentSortPriority(draw_layer)`。第 0 層維持 0，和其他部件同組。
- `OpenKoikatsu.Build.cs` 檢查 `OKOpaqueSortPriority` 標記；`Config/DefaultEngine.ini` 設定 `r.OK.OpaqueSortPriority=1`。

## 4. 驗證

1. 讀卡器開 `C001.png`，連拍 12 張以上，袖子區域像素完全相同；皮帶與袖口在最上層，和現行修正的截圖逐像素比較。
2. `-NullRHI` 自動測試與 `cargo test --release --lib` 通過。
3. CVar 切回 0 時，C001 重新出現閃爍（證明效果確實來自排序，而不是其他改動）。
4. 街區 demo、百人房間、OK Studio 各開一次，確認沒有回歸；記錄 base pass 的 draw 數與 GPU 時間，和 patch 前比較。
5. E 線（`E:\PhysX\UE5.8`）重建後跑同樣的 C001 測試。

## 5. 推出與協調

- Renderer 改動會讓兩份引擎的 UnrealEditor 與 UnrealGame 都要重新連結。
- 街區 stage 需用新的 `--base-dir` 重新打包，舊 stage 仍連到舊 base。
- 動 D: 引擎前先通知 [C組] 街區攻略 session 與 StreetSocialCity 打包，避開它們正在使用 UBT 的時段。
- 不刪除舊的 base／stage。
- 只提交自己的 hunk；引擎 commit 推到 `ok` remote，專案 commit 推到 `origin master`。

## 6. 風險

- **深度位元必須完全相同**：重合層在不同材質下若產生不同的 vertex shader 排列（例如 WPO、PDO），深度可能差 1 ulp，排序就無效。這時 Unity 同樣會 z-fighting，屬於原作行為；但要在驗證時確認 ForgeItem 各實例的 VS 相同。
- **排序成本**：優先度放在最高位，會打散設了優先度的 primitive 的狀態批次。只有重合群組的層和眼睛、頭髮會設，數量很少，預期影響可以忽略，第 4 步實測確認。
- **引擎升級**：每次換引擎版本都要重套 patch；`Build.cs` 的標記檢查會在漏套時讓建置失敗。
