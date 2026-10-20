# 移植計畫：OpenKoikatsu 改用 E:\PhysX\UE5.8，並吸收 OK_Demo

日期：2026-10-15
狀態：階段 0–7 已實作（見 §7 執行紀錄）；階段 8 驗收進行中

## 1. 目標

1. 本專案的引擎從 `D:\UE5.8`（5.8.3，帶本專案的引擎補丁）換成 `E:\PhysX\UE5.8`：
   - 版本 5.8.4，分支 `UE5.8-physX-5.11-pipeline2`
   - PhysX 5.11 放在 GameWorks Flex 外掛
2. `E:\PhysX\UE5.8\Samples\OK_Demo` 做出的整套流程全部收進本專案：okmodel、okclothes、okanim、PhysX 次級動態與比較後端。
3. **OK_Demo 之後不再需要。** 移植完它要退役：歸檔後移除，不留 junction，也不留拷貝。
   完成後本專案必須自給自足，不能有任何東西指向 `Samples\OK_Demo`。
4. **排除 Bullet。** 其餘物理後端都要：PhysX 5.11、KawaiiPhysics、Chaos PBD、Jolt、MuJoCo。
   這些物理只用在 **1 對 1 互動**，群眾模型與群眾流程完全不動。
5. **最後不再需要任何 Unity 檔案。** 資源來源照 OK_Demo 已完成的規則（`OK_BaseLibrary_Plan.md` 的「卡片的資源來源」，C1–C4），原樣搬過來，不重新設計：

| 讀取的卡 | 只能用 | 不能用 |
|---|---|---|
| 我們的 APNG 卡（`okLb`） | OK 基礎庫 + 卡內嵌的 OK 資源包 `okLc` | 遊戲目錄、任何 zipmod、任何 Unity 檔 |
| 原作 KKS 卡 | OK 基礎庫 + **此卡的 zipmod** | 遊戲 `abdata`、其他 zipmod |

   - 卡片專屬資源只在讀原作 KKS 卡時才需要，**不匯入專案**。
     它只存在兩個地方：APNG 卡內的 `okLc`，以及 `%LOCALAPPDATA%\OpenKoikatsu\oklib-cache`。
   - 專案的 Content 只放共用的 OK 基礎庫（`base_v1`），不放任何卡片專屬資源。

## 2. 現況盤點

### 2.1 引擎
- `OpenKoikatsu.Build.cs` 會到引擎原始碼裡找下面四個標記函式：
  - `GetSkeletalComponentMatrixCacheStats`
  - `GetSkeletalFullRefMatrixCacheStats`
  - `SetSkeletalWeightedPoseSeed`
  - `PrepareAnimConstantScaleCache`

  D: 四個都有，E: 一個都沒有。直接切過去，`OK_CROWD_*` 會**默默變成 0**：群眾效能下降，但建置不會報錯。
  補丁在 `Tools\EnginePatches\*` 和 `Tools\Patches\*`。
- E: 本體改動很小：Flex/PhysX 都在 `Engine\Plugins\GameWorks\Flex`，核心只動了 22 個檔（+122/−24）。
  Chaos 照常模擬剛體，不需要關掉。
- E: 工作樹有未提交的 Flex 測試 `.uasset`；`Samples\OK_Demo` 在引擎 git 裡是 untracked，**沒有任何歷史**。
- 約 280 處寫死 `D:/UE5.8`：
  - 約 150 支 `Tools\*.ps1`
  - 根目錄的 `.bat` / `.cmd`
  - 幾支 Python 和 Docs
  - 私有 staged 引擎 `D:\UE5.8\CrowdRuntime{Stable,Development,R2}`

### 2.2 ok-extract 分叉
- OK_Demo 的 `ToolsLocal\ok-extract` 是獨立拷貝；它的 `Tools\` 則是 junction，指回本專案。
- 用歷史 blob 比對推得分叉點約在本專案 `0d0218ae`（2026-10-10）。之後本專案又動了 ok-extract 18 個 commit。
- 對 base 三方分類（忽略 CRLF）：

| 類別 | 檔數 | 處理 |
|---|---|---|
| 只有 OK_Demo 改 | 54 | 直接取 OK_Demo 版 |
| 只有本專案改 | 2 | 保留本專案版 |
| 兩邊都改 | 8 | 手動合併：`lib.rs`、`main.rs`、`session.rs`、`anim.rs`、`anim/shared.rs`、`lowpoly.rs`、`portable_hair.rs`、`vertcolor.rs` |
| 只有 OK_Demo 有（新檔） | 約 7,200 行 | `oklib.rs`、`oklib_convert.rs`、`okmodel_view.rs`、`okmodel_edit.rs`、`okclothes.rs`、`gamefs.rs`、`px.rs`、`anim/disk_cache.rs`、`okanim/`（3,351 行） |
| 只有本專案有 | — | `bin/ok-ramps.rs`；`video_motion/*` 在 base 裡沒有，要另外確認 |

- `lowpoly.rs` 兩邊改動量幾乎一樣（460/458 行），可能是同一份改動先後進了兩邊。合併前先比對兩邊彼此的差異。
- `Cargo.toml` 實際差異只有 OK_Demo 多了 `zstd = "0.13"`，其餘是換行符。
- `vendor\unity-asset-binary` 有 3 檔不同：`asset/parser.rs`、`bundle/parser.rs`、`lib.rs`。

### 2.3 外掛與遊戲模組
| 項目 | 狀況 |
|---|---|
| KKUnityBundle | 16 處差異。OK_Demo 多了 `OKModelEditor` 模組、`OKModelAsset.*`、`KKOkAnimation.*` |
| KKRuntimePose | 3 處：uplugin、Build.cs、`KKRuntimePoseModule.cpp`（PhysX 註冊接點） |
| KKCard | 2 處：`KKCardWorkspace.*` |
| KKSideloader / KKImportedAssets / KKCrowdGpuPacking | 相同 |
| OKAnimation（Runtime / Graph / Editor） | 只有 OK_Demo 有 |
| OKPhysXSecondary | 只有 OK_Demo 有，內含 Bullet / Jolt / MuJoCo / PhysX 的 ThirdParty |
| KawaiiPhysics | 只有 OK_Demo 有 |
| `Source\OpenKoikatsu` | OK_Demo 是舊拷貝，只多 `OKMotionCatalogue.*`、`Tests\OKStreetLongRun.spec.cpp`、`OKStreetSocialRoutine.inl`（要確認是不是已被本專案拆掉的舊檔） |
| `Source\OK_Demo` | `OKDemoGameMode`（675 行）：雙人互動場景、自測、雙人穿模稽核、HTTP 控制、臉部相機、背景/地板/鏡頭指令 |

### 2.4 OK_Demo 其他資產
| 項目 | 大小 / 內容 | 去向 |
|---|---|---|
| `Content\OKAnimation\Family` | 5 個 `.uasset`（KK_Family、骨架、預覽） | 搬進本專案 |
| `Content\OKAnimation\SelfTest` | 約 37 個自測 `.uasset` | 搬進本專案（自測要用） |
| `Content\OKModels` | 4 個 `OK_<id>.uasset` | 不搬，用 `OKModel.Open` 重新匯入（屬可重建產物） |
| `Content\OK_Demo\MotionCatalogue.json` | 1.7 MB | 改由 `build_motion_catalogue.py` 產生，放進本專案 Content |
| `Content\CharacterCards\Demo` | 73 MB（C050.png、Male01.png） | 搬進 `Content\CharacterCards\` |
| `Docs\*.md` | 6 份，1,264 行 | 搬進 `Docs\`，`PhysX_Errors.md` 併入錯誤紀錄 |
| `Scripts\*` | Build、Run、Ctl、RunControl、PairAudit、CompareBackends、BuildPhysicsEngines、build_motion_catalogue.py、mcp/ | 改寫進 `Tools\`（見 §4 階段 6） |
| `.mcp.json` | `ok-demo-control`（port 30110） | 併入本專案現有的 control channel / MCP |
| `ThirdPartyDownloads` | 774 MB：JoltPhysics、KawaiiPhysics、mujoco、bullet3 | 不進 git；建置腳本自己下載固定版本，不帶 bullet3 |
| `Config` | 多 HTTPServer 綁 127.0.0.1、OKAnimation 設定、KKGameRoot、cook 項目；渲染用 SMAA、ScreenPercentage 200 | 只合併功能性設定，渲染維持本專案的 |

## 3. 原則

- 每個階段結束都要能建置、能跑。D:\UE5.8 保留當退路，直到階段 8 驗收通過。
- 規則邏輯放 Rust，UE C++ 只做呈現（照既有慣例）。新增的 Rust 程式進 ok-extract，不另開 crate。
- `Saved\` 一律可丟：產物必須能用腳本重建，不能只放在 `Saved\`。
- 錯誤與預防措施記進 `Docs\STREET_ERRORS.md`；OK_Demo 的 `PhysX_Errors.md` 搬進 `Docs\PHYSX_ERRORS.md`，續用原編號。
- 新增 UI 文字照多語系規則：寫進 `Localization\<lang>\*.toml`，用語意 id 與 ICU 格式。
- 刪除前一律先看清楚目標；含 junction 的目錄絕不遞迴刪除。

## 4. 分階段計畫

### 階段 0：準備與基準
1. 本專案開分支 `physx-port`。這個資料夾還有其他 session 在 `master` 上提交，所以移植工作改在**獨立的 git worktree** 進行，共用的 checkout 保持在 `master`。
2. E: 先**還原**未提交的 Flex 測試 `.uasset`（已決定），再從 `UE5.8-physX-5.11-pipeline2` 開 `UE5.8-physX-5.11-ok`。
3. **先歸檔 OK_Demo**：它沒有 git 歷史，先把整個目錄做成快照：
   - 排除 Binaries、Intermediate、DerivedDataCache、Saved、ThirdPartyDownloads
   - 輸出 `E:\PhysX\Archive\OK_Demo-2026-10-15.zip`，附檔案清單與 SHA256
4. 在 D: 量基準，存進 `Docs\PHYSX_PORT_BASELINE.md`：
   - 群眾 10 / 100 人的 frame time
   - StreetSocial 自測結果
   - `cargo test` 結果
   - OK_Demo 在 E: 上的 `-DemoSelfTest`、`-DemoPairAudit` 結果
5. 確認 `BpGeneratorUltimate`（5.8.0 預編 binary）在 5.8.4 能不能載入；不能就從編輯器 target 停用。

**驗收**：快照可以解開，基準文件完整。✅ 群眾房間（CrowdRoom）因既有重建缺口未量，改以 StreetSocial 自測的 frame 數據為準。

### 階段 1：引擎就緒（E:）
1. 把 `Tools\EnginePatches\*`、`Tools\Patches\*` 逐一套到 E: 分支，每個補丁一個 commit。5.8.3→5.8.4 的衝突逐一修正。
2. 建置並 stage PhysX 5.11：`Engine\Plugins\GameWorks\Flex\Build\PhysX58\Build.ps1 -Stage`。
3. 建置 `UnrealEditor`、`ShaderCompileWorker`、`UnrealPak`。
4. 新增 `Tools\check_engine_patches.ps1`，`OpenKoikatsu.Build.cs` 同步修改：任何標記缺少就建置失敗，不再默默降成 0。
5. 把「E: 引擎要套哪些補丁」寫成 `Tools\EnginePatches\README.md` 裡的清單，之後換引擎版本時照表重做。

**驗收**：E: 編輯器能啟動；四個標記在建置 log 都是 1。

### 階段 2：引擎路徑集中化
1. 新增 `Tools\ok_engine.ps1`（給其他腳本 dot-source 用）。
   - 引擎根目錄的解析順序：
     1. `-Engine` 參數
     2. 環境變數 `OK_ENGINE_ROOT`
     3. `Tools\engine.local.json`（不進版控）
     4. 預設 `E:/PhysX/UE5.8`
   - 對外提供：`$OkEngine`、`$OkEditor`、`$OkEditorCmd`、`$OkBuildBat`、`$OkUbt`、`$OkUat`、`$OkUnrealPak`。
2. 寫一支一次性的 Rust 小工具或 PowerShell 腳本，機械式替換：
   - `$Engine='D:/UE5.8'` 這類預設值
   - `Start-Process 'D:/UE5.8/...'`、ExecutablePath 比對等字面路徑

   替換完再 grep 確認零殘留（`D:/UE5.8`、`D:\UE5.8`、`D:\\UE5.8`）。
3. `.bat` / `.cmd` 改成只呼叫 ps1；Python 改讀 `OK_ENGINE_ROOT`；`VsmMarkingQueueReuse.meta.json` 改成相對於引擎根目錄的路徑。
4. `OpenKoikatsu.uproject`：`EngineAssociation` 設為 E: 的 GUID `{7A2A60EB-4330-A41B-0900-5BBEFDA5A0A5}`。
5. 私有 staged 引擎（CrowdRuntime*）改成由腳本從 E: 重建，放到 E: 底下的新 tag 目錄，不覆蓋舊的。
6. 獨立的 `.uproject`（`Tools\CrowdBehaviorBake`、`GpuPoseAudit`、`PoseBoneAudit`）一併改。
7. README、CONTRIBUTING、CARD_READER_DEVELOPMENT、PHASE1_PLAN 中「只能用 D:\UE5.8」的規定都改掉。

**驗收**：grep 零殘留；`Tools\ok_engine.ps1 -Print` 印出正確路徑；Editor target 用 E: 建置成功。

### 階段 3：ok-extract 三方合併
1. 工具：`git merge-file`，base 用 `0d0218ae`，ours 是 `Tools\ok-extract`，theirs 是 `ToolsLocal\ok-extract`。
2. 照 §2.2 的分類依序合併，每批跑一次 `cargo test`：
   1. 新檔（oklib、okmodel、okclothes、gamefs、px、okanim、disk_cache），加上 `Cargo.toml` 的 zstd。
   2. 只有 OK_Demo 改的 54 檔。
   3. 兩邊都改的 8 檔，手動合併。`lib.rs`、`ffi.rs` 的 FFI 匯出要同步到 `KKRustExtract.h`。
   4. `vendor\unity-asset-binary` 的 3 檔。
3. 確認 `video_motion/*` 和 `bin/ok-ramps.rs` 這些本專案獨有的功能沒有被覆蓋。
4. 合併後統一換行符（照 `.gitattributes`）。
5. CLI 驗收（指令取自 OK_Demo 文件）：
   - `--oklib probe`、`models`、`sources`、`replay`、`card`、`kks-verify`、`okcard-verify`
   - `model-view`、`model-texture`、`model-diff`
   - `clothes-import`、`clothes-export`、`clothes-wear`、`clothes-test`
6. 確認 Base Library 的位置：`Content\IndependentResources\base_v1` 已在本專案，`OK_BASE_LIBRARY` 預設指向它；
   快取仍放 `%LOCALAPPDATA%\OpenKoikatsu\oklib-cache`。

**驗收**：`cargo test` 全綠；上面的 CLI 全部通過；StreetSocial 現有行為不變（ok-rules / ok-mind 的測試照跑）。

### 階段 4：外掛合併
1. **KKUnityBundle**
   - 合併 16 處差異：KKRustExtract、KKPairedPlayback、KKPreviewActor、KKPreviewRuntimeMaterials、KKSkeletalBuilder、KKUnityBundleModule、Build.cs、uplugin。
   - 新增 `OKModelAsset.*`、`KKOkAnimation.*`，以及 Editor 模組 `OKModelEditor`：`UOKModelImportFactory`、`UAssetDefinition_OKModel`、`FOKModelEditor`。
   - Build.cs 指向本專案的 `Tools\ok-extract`，不再有 `ToolsLocal`。
   - 規則照舊：只有 KKUnityBundle 直接連結 `ok_extract.lib`，其他模組都走 `FOkAnimBridge`。
2. **KKRuntimePose、KKCard**：合併差異；`OKPhysXSecondary::Register(&ok_set_physx_dynamic_solver)` 的呼叫點兩邊都要保留。
3. **新增外掛**
   - OKAnimation（Runtime、Graph、Editor）
   - KawaiiPhysics（v1.21，保留原授權檔）
   - OKPhysXSecondary
4. **OKPhysXSecondary 移除 Bullet**
   - 刪掉 `OKBulletBackend.cpp` 和 `Source\ThirdParty\Bullet`。
   - Build.cs 拿掉 Bullet 的 include、lib，以及「缺 lib 就丟錯」的檢查（Jolt / MuJoCo 的檢查保留）。
   - `OKSecondaryBackends.cpp` 移除 Bullet 派送（例如 `OKBulletRelease`）。
   - 後端編號不重排：0 PhysX、1 Kawaii、2 Chaos、4 Jolt、5 MuJoCo，3 保留不用。
     `Backend` 或 `SettleBackend` 設成 3 時記一次警告並改用 0。cvar 說明同步更新。
   - Bullet 的授權檔與 staging 項目移除。
5. **Build.cs 路徑**：PhysX lib 的寫死路徑 `E:\PhysX\UE5.8\Engine\Plugins\GameWorks\Flex\...` 改用 `EngineDirectory` 推導。
   找不到時，錯誤訊息指向 `Build.ps1 -Stage`。
6. **`.uproject`** 啟用：OKAnimation、OKPhysXSecondary、KawaiiPhysics、ControlRig、ACLPlugin、EnhancedInput、PropertyAccessNode、LevelSequenceEditor。
   不開 `DisableEnginePluginsByDefault`：本專案編輯器還需要 ModelingToolsEditorMode。

**驗收**：所有 Editor target 建置成功；`OKModel.EditorSelfTest` 通過；`OKPhysXSecondary.Backend` 0/1/2/4/5 都能切換，設 3 時會退回 0。

### 階段 5：遊戲模組、設定、資產
1. **遊戲模組**
   - 搬入 `OKMotionCatalogue.*`、`Tests\OKStreetLongRun.spec.cpp`。
   - `OKStreetSocialRoutine.inl` 先比對，若本專案已拆進其他檔就不搬。
2. **`OKDemoGameMode` 的功能拆進本專案**，不搬 OK_Demo 模組本身：
   - 雙人互動場景 + 自測 → Interaction 模式的啟動參數：`-PairSelfTest`、`-PairAudit*`，對應原本的 `-DemoSelfTest`、`-DemoPairAudit*`。
   - HTTP 控制（exec / reset / stats / measure / pose）→ 併入本專案現有的 control channel，不另開 30110 埠。
   - 臉部相機、背景、地板、鏡頭環繞指令 → 改成 `ok.Interaction.*` console 指令。
   - `ok.Demo.PairBatch` → `ok.Interaction.PairBatch`。
3. **Config 合併**
   - DefaultGame：
     - OKAnimation 設定（`DefaultFamily`、`AssetRoot`、`ImportFps`）
     - cook / NonUFS 項目：PairedAnimations、BodyInteractions、KKPreview
     - `KKGameRoot` 若本專案已有就沿用
   - DefaultEngine：
     - `HTTPServer.Listeners` 綁 127.0.0.1
     - `r.GPUSkin.UnlimitedBoneInfluences`、`Support16BitBoneIndex` 若沒開就補上
     - 抗鋸齒、ScreenPercentage 維持本專案的設定
4. **Content**
   - 搬 `OKAnimation\Family`、`OKAnimation\SelfTest` 和 Demo 卡片。
   - `MotionCatalogue.json` 改由腳本產生。
   - `OKModels` 不搬，需要時重新匯入。
   - ⚠ 這些 `.uasset` 是 5.8.4 存的，搬進來後 D: 就開不了：這一步是單向的，所以排在階段 1–4 都通過之後。

**驗收**：Interaction 模式能用雙人自測跑過；本專案的 control channel 可以下 pose / measure / stats。

### 階段 6：腳本、工具、MCP
| OK_Demo | 本專案 |
|---|---|
| `Scripts\Build.ps1` | 併入既有建置腳本，用 `ok_engine.ps1` |
| `Scripts\Run.ps1` | `Tools\run_interaction_pair.ps1`（`OK_GPU_SKIN=1`、`OK_SKELMESH=1` 等環境變數照帶） |
| `Scripts\Ctl.ps1`、`RunControl.ps1` | 併入 street_play / street_playtest 的 control 工具 |
| `Scripts\PairAudit.ps1` | `Tools\run_pair_audit.ps1` |
| `Scripts\CompareBackends.ps1` | `Tools\compare_secondary_backends.ps1`（無 Bullet） |
| `Scripts\BuildPhysicsEngines.ps1` | `Tools\build_physics_backends.ps1`：下載固定版本的 Jolt 5.6、MuJoCo 3.15、KawaiiPhysics 原始碼，驗 SHA256 後建置，**不含 Bullet** |
| `Scripts\build_motion_catalogue.py` | 改寫成 Rust（ok-extract 的子命令 `motion-catalogue`）；交付工具不用 Python |
| `Scripts\mcp\ok_demo_mcp.py`、`compare_tuned.py` | 功能併入 `Tools\character_mcp\rust`，Python 版僅作開發參考 |

`ThirdPartyDownloads` 不進 git：
- 下載路徑：`Tools\.thirdparty\`（加入 `.gitignore`）
- 建置產物：直接寫進 `Plugins\OKPhysXSecondary\Source\ThirdParty\{Jolt,MuJoCo}\lib`

### 階段 7：流程接通
1. **角色**
   - 流程：卡片 → oklib 重播 → `.okmodel` → UE 骨架網格 → `px.rs` 抽出 DynamicBone 鏈 → OKPhysXSecondary 解算 → 回寫骨骼。
   - 只接 1 對 1 互動（Interaction 的雙人場景，以及 StreetSocial 進入雙人互動時）。
2. **群眾**：不接 PhysX，群眾模型與流程完全不動。
3. **PhysX 不可用時退回**：CUDA 或 GPU 初始化失敗，就退回 Kawaii，再不行退回 Rust `dbone`，只記一次 log。
4. **服裝**
   - `.okclothes` 先接 CLI，再接 Gallery 服裝 UI（`OKGalleryClothing` 的匯入 / 匯出 / 穿上）。
   - 文字放進三語系的 `ui_text.toml`。
5. **OKModel 編輯**：`.okedit.json` 的覆寫要套用到所有引用該模型的卡片。補上 OK_Demo 還沒在 UE 自測過的兩項：貼圖替換、網格變換。
6. **雙人互動**：開啟角色間碰撞。用 99 個雙人姿勢稽核當回歸測試。
7. **讀卡資源規則**（照 OK_Demo C1–C4 原樣搬）
   - 規則本身見 §1 第 5 點。
   - 實作上：`gamefs` 的後備只在讀 KKS 卡時打開；`ok_session_has_game_root` 決定 portable；`LogOkExtract` 記錄每張卡的資源來源。
   - 本專案所有讀卡入口都改走這條路，包括 Gallery、讀卡器、Interaction、StreetSocial。
     遊戲目錄（`KKGameRoot`）只用來找「此卡以 GUID 指名的 zipmod」，不再讀 `abdata`。
8. **Runtime host**
   - `prepare_interaction_runtime_host.lua` 加入新外掛。
   - stage 時帶上 PhysX DLL、`mujoco.dll` 和授權檔。
   - Shipping 版預設只帶 PhysX、Kawaii、Chaos；Jolt、MuJoCo 只在 Development 帶。

### 階段 8：驗收
1. 所有 target 建置通過：Editor、CrowdEditor、CrowdEngineEditor、CrowdPacked、Interaction runtime host。
2. 四個引擎補丁標記都是 1；群眾 frame time 不劣於階段 0 的基準（容許誤差 3%）。
3. `cargo test`（ok-extract、ok-rules、ok-mind）全綠；階段 3 的 CLI 驗收全部通過。
4. 雙人自測、99 個姿勢的穿模稽核，結果不劣於 OK_Demo 在 E: 上的基準。
5. 後端比較腳本跑完 0/1/2/4/5 五種後端。
6. StreetSocial：control channel 自測，加上一輪 zh-CN 攻略流程。
7. 讀卡資源規則（照 OK_Demo 的驗收方式）
   - APNG 卡：封閉重播（沒有遊戲目錄、後備關閉）的 digest 要等於遊戲模式。
   - KKS 卡：基礎庫 + 此卡 zipmod 的 digest 要等於遊戲模式。
   - 遊戲內兩種卡都能開，也能換衣服（OK_Demo 還沒測換衣服）。
   - 遊戲目錄改名或移走後，APNG 卡照樣能開。
8. 全專案 grep 零殘留：`OK_Demo`、`ToolsLocal`、`Samples\OK_Demo`、`D:/UE5.8`。

### 階段 9：整合完成後收尾：移除 Unity 檔、OK_Demo 退役、D: 除役
1. 確認階段 0 的快照還在，而且階段 8 全部通過。
2. **專案內不留任何 Unity 檔**
   - 列出 Content 與 Tools 裡所有 Unity 來源的檔案（`.unity3d`、`abdata`、`.zipmod` 等），以及仍在讀它們的程式碼。
   - 先改掉讀取端，再刪檔，每一項都要先問過。
   - 刪完重跑階段 8 第 7 項的讀卡驗收。
3. **刪除 `E:\PhysX\UE5.8\Samples\OK_Demo` 前先問過使用者。**
   刪除前要先移除裡面的 junction（`Tools`、`Content\IndependentResources`、`KKPreview`、`Koikatsu`、`PaintDemo`、`UI`）：
   - 只拆 junction 本身，不碰它指向的本專案資料
   - 拆完確認本專案的 `Content` 和 `Tools` 完好
   - 才刪其餘部分
4. E: 的 Flex 相關文件（`Engine\Plugins\GameWorks\Flex\README.md`、`Build\PhysX58\*.md`）留在引擎裡。本專案的 Docs 只連結過去。
5. D:\UE5.8 不再是預設路徑。目錄保留不刪，備用一段時間後再另行決定。
6. 更新記憶與文件：StreetSocial 的建置改走 E:。

## 5. 風險與對策

| 風險 | 對策 |
|---|---|
| 引擎補丁在 5.8.4 上套不上 | 放在階段 1 最先做；套不上就停下處理，不往下走 |
| ok-extract 合併衝突 | 只有 8 檔真衝突；分批合併，每批跑 `cargo test`；FFI 簽名一律同步到 C++ 標頭 |
| 5.8.4 存的資產無法回到 D: | 資產搬移排在階段 5 之後；切換前打 git tag |
| OK_Demo 沒有 git 歷史，刪了就沒了 | 階段 0 先做快照 zip 加 SHA256，階段 9 刪除前先問 |
| junction 遞迴刪除會刪到本專案資料 | 階段 9 先拆 junction，確認目標完好後才刪 |
| PhysX 5.11 需要 CUDA | 偵測失敗就退回 Kawaii，再退回 `dbone` |
| BpGeneratorUltimate 預編 binary 不相容 | 編輯器 target 停用或重建，不影響遊戲 |
| 授權 | PhysX（BSD-3）、Jolt（MIT）、MuJoCo（Apache-2.0）、KawaiiPhysics（MIT）的授權檔隨 stage 帶出 |
| 埠衝突（30110 與既有 control channel） | 只保留本專案的 control channel |

## 6. 已定事項（2026-10-15 確認）

- 預設引擎路徑：`E:/PhysX/UE5.8`；可用 `OK_ENGINE_ROOT` 覆寫。
- PhysX 等物理只用於 1 對 1 互動；群眾不受影響。
- Jolt、MuJoCo 只在 Development 版帶；Shipping 版只帶 PhysX、Kawaii、Chaos。
- OK_Demo 的 `OKModels` 匯入結果不搬，需要時重新匯入。
- OK_Demo 的 Python 工具改寫成 Rust；Python 版只作開發參考，不交付。
- E: 上未提交的 Flex 測試 `.uasset`：還原。
- OK_Demo 在整合完成（階段 8 通過）後才刪，刪除前仍要先問。
- 讀卡資源規則照 OK_Demo 已完成的做法搬，不重新設計；卡片專屬資源不進專案。

## 7. 執行紀錄（2026-10-15）

| 階段 | 結果 |
|---|---|
| 0 | 完成。基準見 `PHYSX_PORT_BASELINE.md`；OK_Demo 快照 `E:\PhysX\Archive\OK_Demo-2026-10-15.zip` |
| 1 | D: 的 9 個引擎 commit 以 `git format-patch` / `git am -3` 搬到 E: 的 `UE5.8-physX-5.11-ok`，全部乾淨套上。四個標記 E: 都有。UnrealEditor / ShaderCompileWorker / UnrealPak 建置成功。`OpenKoikatsu.Build.cs` 改成缺補丁就失敗（可用 `OK_ALLOW_UNPATCHED_ENGINE=1` 放行）；清單見 `Tools/EnginePatches/README.md` |
| 2 | `Tools/ok_engine.ps1` 統一提供引擎路徑；161 支 ps1、2 個啟動檔、5 支 Python 改用它（`OK_ENGINE_ROOT`）。`.uproject` 綁 E: 的 GUID。有日期的歷史紀錄、`seal_*`、`.ai-collab` 保留原文 |
| 3 | ok-extract 三方合併：63 檔取 OK_Demo 版、16 新檔、8 檔自動合併，0 衝突。修了兩處：`gamefs::DirEntry::metadata` 改成不跟隨 junction（和 std 一致），`head_look` 測試骨架補上 OK_Demo 新做法需要的胸、肩骨。`cargo test`：467 通過，失敗的 48 個與基準完全相同。APNG 卡封閉驗收 3/3 MATCH，digest 與 OK_Demo 紀錄一致 |
| 4 | KKUnityBundle、KKRuntimePose、KKCard 合併（OKModelEditor、OKModelAsset、KKOkAnimation）。新增 OKAnimation、KawaiiPhysics、OKPhysXSecondary。Bullet 已移除：後端 3 保留不用，選到會退回 PhysX。PhysX DLL 改由 Build.cs 在建置時從引擎 Flex 目錄複製，`mujoco.dll` 放在 `Source/ThirdParty/MuJoCo/bin`。OKPhysXSecondary 與 OKAnimation 關掉 unity build：OK_Demo 的檔案在引擎 git 裡是 untracked，一直以 non-unity 編譯，從沒遇過合併編譯的撞名 |
| 5 | `OKDemoGameMode` 改寫成 `AOKInteractionPairGameMode`（`-Pair*` 參數、`ok.Interaction.*` 指令、控制埠 47871）。動作目錄移到 `Content/IndependentResources/MotionCatalogue/`（NonUFS）。OKAnimation 資產與 Demo 卡片已搬入。HTTP 監聽只綁 127.0.0.1 |
| 6 | 腳本：`run_interaction_pair`、`run_pair_audit`、`pair_ctl`、`compare_secondary_backends`、`build_physics_backends`（固定 commit、不含 Bullet）。動作目錄產生器改寫成 `ok-extract . --motion-catalogue`，輸出與 Python 版逐位元組相同。調參 MCP 改寫成 Rust 的 `ok-pair-mcp`（含 `compare` 子命令）。6 份文件已搬入 `Docs/` |
| 7 | PhysX 只給 1 對 1 互動用：Rust 端依 session 決定（`ok_session_set_physx_secondary`，預設關閉，有單元測試）；C++ 端由 `AKKPreviewActor::AddPairPhysics` 參考計數，雙人播放與互動場景各自登記。群眾、StreetSocial NPC、Gallery 都維持原本的 DynamicBone。Shipping 版只帶 PhysX、Kawaii、Chaos。Gallery 服裝頁新增 `.okclothes` 匯出、穿上、轉換三個按鈕 |
| 8 | Editor 用 E: 建置成功（含 BpGeneratorUltimate）。`cargo test`：ok-extract 469 通過，失敗的 48 個與基準相同；ok-rules 149、ok-mind 37 全過。互動自測挑到的 5 個動作都進入播放，和 OK_Demo 基準相同。雙人稽核 99/99 都有量測結果：平均穿入 0.0230（基準 0.0467），最大 0.106（基準 0.119）；基準的違規數原本被暫時 session 的幽靈身體灌高，見 `PHYSX_ERRORS.md`。2026-10-15 起角色碰撞的後續驗證由使用者自行處理，後端比較沒有跑完。逐姿勢資料：`Docs/PhysXPortBaseline/port-pairaudit.tsv` |

## 8. 與 Unity 檔脫鉤（2026-10-16）

目標：只有讀原作 KKS 卡的個人 zipmod 時才碰 Unity 檔，其他一律走 OK 資料。

| 項目 | 狀態 |
|---|---|
| 群眾讀卡（`KKRuntimePose` 的 `session_open("", 卡)`） | 支援 OK 卡（`okLb`）。模組啟動時就設好 `OK_BASE_LIBRARY`，不再依賴先呼叫 `FOkSession::Open` |
| 平行讀 OK 卡 | 修好競態：`oklib::ensure_library` 加鎖，store 與 gamefs 都就緒才算啟用。之前前幾張卡會讀成 `game_root missing abdata` |
| OK 卡封閉重開 | 修好轉換器漏洞：錄製卡內資源包時，同時錄 bundle 的貼圖目錄（`texture_directory2`）。之前 KK_107311 封閉重開缺 3 筆。另外讓清 bundle 快取能清到所有執行緒 |
| OK 卡的 uncensor | 從基礎庫讀的 session 不再看專案的 `uncensor_v1`（`gamefs::serves_from_library`） |
| 編輯器的 KKS 卡匯入（`OKResolveCardSource`） | 只掛這張卡的 zipmod，原版資料來自基礎庫，不再用 `native_v1` |
| `native_v1`（22 GB `.unity3d`） | 已無程式讀取，搬到 `E:\PhysX\Archive\UnityResources\native_v1`（972 檔，可搬回） |
| `uncensor_v1`（zipmod） | 內容已全部轉進 OK 基礎庫：25 個 zipmod 中原本缺 4 個，補轉了 BP balls color match 兩個與 DeathWeasel color matching balls；NewBie Kirara body 已不在遊戲目錄，從 `uncensor_v1` 的那份放回遊戲 `mods/KKSImportedMods/作者系列含mod2/NewBie/mod/Kirara_body.zipmod` 後轉入。UncensorSelector 設定與遊戲那份相同。OK 卡不讀 `uncensor_v1`；只剩舊 `okPv` 卡（`CrowdAPNG` 102 張）還在用，從原始卡轉成 OK 卡後即可移走 |
| 群眾卡（`Content/CharacterCards/CrowdAPNG`） | 舊 okPv APNG 卡與衍生檔（`.verified.json`、`.lowpoly.*`、`.okstates`）全部刪除；`Custom/` 的 GoldenHair 舊卡刪除。改由 `D:\KoikatuSunshine\UserData\chara\female` 根目錄的 KKS 原作卡依檔名順序轉成 OK 卡 C000–C099，來源記在 `sources.tsv`，`selection.json` 同步更新。StreetSocial 男主角（`output/StreetSocialAPNG/Male01.png`）與雙人 Demo（`Demo/Male01.png`、`Demo/C050.png`）也換成 OK 卡。工具：`Tools/convert_crowd_okcards.lua`（list / install） |
| 轉換速度 | 單張由 73 秒降到約 13 秒：基礎庫模式下 `override_index` 每次查詢都打開全部 7,285 個 zip 重建、`fallback_listing` 每列一個目錄就重建一次已錄 zip 集合，改為程序內快取（`gamefs::library_memo`／`library_zip_memo`）。新子命令 `--oklib cards <lib> <清單> <輸出>`（一個程序連續轉），12 個程序平行時 115 張共 465 秒 |
| 群眾卡驗收 | 新子命令 `--oklib okcard-check <lib> <OK卡目錄> <清單> live`。115 張的 OK 卡封閉重開（只有 OK 資料）全部等於原作卡的遊戲模式 digest。有 2 張（兔田佩克拉、kks_重绘琪亚娜）製作時記下的 digest 不同：製作那次查不到模型 `ae7b25…` 的 `prefab_directory`，飾品挑到別的來源；OK 卡本身正確，這個值也沒有被使用，製作流程的缺口待修 |
| `uncensor_v1` | 已移除：讀取端（Rust pack 路徑、`OK_UNCENSOR_RESOURCES`、GPU 快取鍵、匯入腳本、舊 okPv 的 genital 補丁）全部拿掉，目錄搬到 `E:\PhysX\Archive\UnityResources\uncensor_v1`（27 檔，可搬回） |
| uncensor 驗證 | 基礎庫更新後 10 張女卡、1 張男卡的 OK 卡驗證全部 MATCH、0 缺口。男卡第一次驗時發現 genital 部位順序不固定（HashMap），已修 |
| GPU 姿勢快取 key | 加入基礎庫的 `converted.tsv`，基礎庫更新時舊快取會失效 |
| 驗證 | 10 張由原始卡轉出的 OK 卡：轉換驗證全部 MATCH、0 缺口。StreetSocial 用其中 5 張：快取熱了之後選角到 GPU 就緒 17 秒，舊卡約 15 秒 |

| 西瓜互動（較矮的 OK 卡打不到） | 修好：西瓜距離與 contact IK 目標依打擊者肩高等比縮短（只縮不放），脊椎上限 30°→45°。10 張 OK 卡離線誤差都在 5.5 cm 內；StreetSocial 自測 A 組 10 個西瓜案例全中（最差 7.85 cm，門檻 12.4 cm），整體 passed，frame 平均 6.75 ms |
| 10 個角色驗收（`Saved/OKCardTest/selection_a.json`、`selection_b.json`） | A、B 兩組 StreetSocial 自測都通過（B：0 錯誤，西瓜最差 6.01 cm，frame 平均 6.40 ms）。B 第一次偶發「一起散步沒停在玩家旁邊」，重跑未重現，已加診斷並記在 `STREET_ERRORS.md` |
| OKModel 編輯器 | 開啟卡住已修（`okmodel_view::with_library` 不再每次呼叫都重新啟用基礎庫）。`OKModel.EditorSelfTest` 全部 PASS（預覽、材質編輯與還原、貼圖）。冷啟動第一次開模型約 5 分鐘（載入基礎庫索引） |
| 殘留路徑 | `OK_Demo`、`ToolsLocal`、`D:/UE5.8` 只剩註解裡的來歷說明。`check_vsm_queue_reuse.lua` 改用新的 `ok.engine_root()`（與 `ok_engine.ps1` 同順序） |

注意：OK 卡第一次開啟要從基礎庫解碼（每張約 1 分鐘），之後走開卡快取（`%LOCALAPPDATA%\OpenKoikatsu\oklib-cache`）。轉完卡後要先開一次預熱快取。

OK_Demo 已退役：junction 先拆，再刪除。快照在 `E:\PhysX\Archive\OK_Demo-2026-10-15.zip`。
