# 外觀選擇器修復與 0.3 秒切換驗收交接

更新日期：2026-10-10。工作目錄：`C:/portable/OpenKoikatsu`。

## 一、接手先讀：完成狀態

**整體尚未完成，不能宣稱全部選擇器已通過。**

- 使用者要求所有能顯示範例圖的功能都能套用；依 registry 順序，每個 target 實際選十個不同項目，完成後才換下一類，且每次點擊至畫面更新小於 300ms。
- 目前 registry 共 300 個 target，完整驗收需 3000 次實際 UI 選擇，還沒有完成這一輪。
- 目前部署版本以 R108 記錄為準：11 個頭部 target、110 次實際 UI 選擇全部顯示／儲存／速度通過，最大 179.698098ms。這三段是診斷子集，不能合併冒充完整依序驗收。
- Native 原始碼 R115 已通過飾品單槽準備、原子發布、版本拒絕、checkpoint、reset、其他服裝不變等整合測試，但尚未連到 UE 飾品局部顯示。
- Native R117 的實際 C001 APNG 七套服裝回歸已通過，4.37 秒，來源 SHA256 不變。
- **最新原始碼又比 R117 多了少量 FFI 骨架，尚未編譯。** 目前沒有新增完整 geometry C ABI 匯出函式，也沒有新增 C++ wrapper 或 UE staging 接線。請勿把 R117 執行檔結果套用到這些後續改動。

## 二、使用者約束與工作邊界

1. 通用 runtime 解法，角色由 APNG 讀入，不能依賴角色特化預烘焙資源；可使用共用原作資源。
2. 不再詢問確認；遇到問題自行分析、修正並記錄 MD，最後做完整驗收。
3. 驗收必須實際畫面生效和保存正確，不能只驗 metadata 或 native 成功。
4. 300ms 計時包含整個點擊至 render-ready，不能扣除排程、I/O、等待，也不能靠預選前十項、加 sleep 或放寬門檻過關。
5. 本階段是編輯器外觀選擇器。更早的一對一互動場景、120fps、水槍等需求保留，但不是這次驗收的替代項目。
6. Git 工作區有大量既有修改／未追蹤檔，含其他工作；不要 reset、clean、批次還原或刪除。
7. 不修改 `D:/UE5.8` 引擎原始碼；模組建置使用 `-NoEngineChanges`。
8. 不終止使用者或其他工作程序。先前使用者 GoldenMCP PID63484 僅供背景脈絡，接手時重新確認，不可因舊 PID 直接操作。
9. 本工作未建立 goal、未啟動 subagent、未 commit／push。使用者提供的 AGENTS.md 要求主 agent 對驗證／整合負責。

## 三、先讀文件與確定證據

- 計畫與歷史：`C:/portable/OpenKoikatsu/Docs/APPEARANCE_SELECTORS_FIX_20261010.md`
- 錯誤記錄：`C:/portable/OpenKoikatsu/Docs/CHARACTER_EDITOR_ERRORS.md`
- 本交接：`C:/portable/OpenKoikatsu/Docs/APPEARANCE_SELECTORS_HANDOFF_20261010.md`
- 真正 UI 結果：`C:/portable/OpenKoikatsu/Saved/Tests/AppearanceSelectors20261010/head-r108-summary.json`
- R115 九組 native 結果：`C:/portable/OpenKoikatsu/Saved/Tests/AppearanceSelectors20261010/NativeR115/summary.json`
- R117 APNG 測試：`C:/portable/OpenKoikatsu/Saved/Tests/AppearanceSelectors20261010/native-runtime-apng-r117.log`
- R117 編譯輸出：同目錄 `native-build-r117.jsonl`、`native-build-r117.log`
- 固定 R117 測試程式：同目錄 `test-binaries/appearance-r117.exe`，大小 31,346,688 bytes。
- R115 測試執行檔 SHA256：`5D9A138E3DE732D60601031045F60E8B2802E1AC2501124B6F0B205F36576945`。

### 已部署 R108 的 UI 子集

| Tag | 次數 | 平均 ms | 最大 ms | 顯示／保存／速度 |
|---|---:|---:|---:|---|
| AppearanceSweep20261010HeadTypesR18 | 70 | 103.773637 | 159.496501 | 全通過 |
| AppearanceSweep20261010LeftEyeR108 | 20 | 127.699495 | 179.698098 | 全通過 |
| AppearanceSweep20261010RightEyeR108 | 20 | 123.997280 | 173.219997 | 全通過 |

三次 canonical DLL path/hash 相同。各 report 在 `Saved/Tests/EditorAcceptance/<Tag>/report.json`。診斷子集 `valid=false`、`complete=false`、runner exit1 是刻意維持完整驗收要求；檢查每列結果，不要修改成 full pass。

重要欄位：`click_to_render_ready_ms`、`under_300ms`、`preview_and_selection_applied`、`saved_field_and_source_verified`、`valid`。

## 四、已找出的根因與修正

### 頭部偶發卡頓：同步 stderr 寫入

R104 HeadTypesR17 70 次均顯示／保存成功，但有一筆 462.919ms。詳細 source stages 直接量到 `detail.log=356.780ms`，其餘 bundle0.393／override0.159／texture6.248／flip1.221／tint1.339ms。這是已量到的根因，不要再把它概括成 CPU 或解析大檔慢。

`Tools/ok-extract/src/lib.rs` 的 `resolve_head_detail_texture` 三處正常成功日誌改成只有 `OK_DIAG` 或 `OK_APPEARANCE_PROFILE` 開啟才輸出；錯誤日誌保留。JSON 階段計時無同步 log I/O。R108 的 110 次 UI 驗證已證實這段改善。

### 驗收截圖必須等實際完成

`Source/OpenKoikatsu/Tests/OKEditorAppearanceSweep.cpp` 的 FRun 擁有 screenshot request，Stage7 計時後才截圖，等 `OnScreenshotRequestProcessed` 並驗證 FileSize>0 才進下一次點擊／保存，記錄 `screenshot_saved` 和 `audit_screenshot_ms`。Stop 清 delegate。不能用固定 sleep，也不要讓上一張截圖 I/O 混入下一次選擇。

### 原始來源與 APNG 顯示的 shape policy 不同

R113 geometry Context 首次整合因 shape guard 失敗。來源萃取包含 PushUp 等 plugin；APNG display 使用 editable Custom(false)。R115 抽出 `portable::apply_editable_shape_parameters`，讓 APNG load 和 geometry candidate 共用 sex／shape count／finite／coordinate-local ABMX／bust soft-weight／apply_shape／skin_all 規則，candidate 再 refresh、建立 reference export。保留嚴格 shape／rest rig guards，不能移除驗證繞過問題。

## 五、已實作但還沒接上 UI 的 native geometry

### `Tools/ok-extract/src/skel_export.rs`

- `PartExports`：morphs、rest、normals、tangents、bone_idx 五組 per-part arrays。
- `prepare_part_exports` 與完整 `build` 共用 `bake_part_exports`，只對新增 renderer 做 reference-space bake。
- `replace_part_exports` 在 mutation 前檢查五組 counts 與完整連續 retained map；move 舊 buffers，保留 pointer identity；不改 global bones/name_to_idx。
- skel_kind4 的 private bone_idx 契約不能當成 global indices。
- 單元測試驗證錯誤 map／array 不會部分修改、retained allocation 保持、reset。
- installed integration 驗證每個 source renderer 的 part-local 結果等於完整 export。
- 注意 `build` 仍有正常 `gpu skel bones=...` eprintln；新 geometry 熱路徑正式驗收前要量測並處理成功日誌，避免重犯同步 stderr 問題。

### `Tools/ok-extract/src/session.rs`

`replace_accessory_parts(outfit,slot,retained,added,exports)`：

- 驗證 outfit/count、精確 `outfit{n}.accessory{slot}` provenance、恰好只移除目標槽位、新零件 role/property、global export 存在、完整 retained map。
- 原子驗證 per-part exports 後才移動 session parts；保留其他零件及其 private dynamics、全域 rig 和 animation clock。
- 舊 APNG 缺 provenance 時明確拒絕，尚待通用 runtime upgrade。

### `Tools/ok-extract/src/appearance/live_head.rs`

新增 `PreparedGeometry`，持有 revision、target/item/slot、owned source OkSession、retained map、added range、timings、raw metadata、全 outfit snapshots。

- `Context::prepare_geometry`：accessory121..130，精確 catalog/source identity；patch 當前 raw；resolve 優先本次 snapshot.unique 再 baseline shared source，連續修改不需中途寫檔。
- 只準備新增零件的 owned source session，套用共用 APNG display shape policy；準備期間無 live publish／card file write。
- `validate_geometry`：revision、shape、map layout、global name/index、bone rest pose／parent／locked 嚴格一致。
- `geometry_checkpoint`：提交前釘住完整 next revision／所有 outfit 保存內容。
- `commit_geometry`：validation 和 category lookup 在 mutation 前，發布 native 零件後才更新 Context raw/snapshots/dirty/revision。
- 缺舊 APNG context 回報 `geometry transaction requires a runtime legacy provenance upgrade`。這是未完成項目，不是最終 fallback。

### filename-free source extraction

`extract_session_from_info(game,None,outfit,None,Some(CardInfo))` 與路徑版共用完整 pipeline，無 temporary source PNG 或 filename SQL cache；accessory context/provenance 根據 metadata 產生。Fresh path 和 filename-free source 的 geometry、UV、weights、bind pose、material、private contract、context 全部 parity 已通過。

**不要把 SQL cache 的 postprocessed baseline 與 fresh source 比較。** 之前這樣測造成 bind pose 差異；fresh 對 fresh 已通過。SQL legacy compatibility 仍是另一件未查完的事，不要刪資料庫掩蓋。

## 六、最新這一輪未編譯的改動（接手第一件事）

在 R117 測試完成之後，只編輯了：

1. `appearance/live_head.rs`：`PreparedGeometry.raw` 改為 `pub`，供 sibling FFI 存取。
2. `appearance/live_head_ffi.rs`：import PreparedGeometry；新增 `SourceCommand::PrepareGeometry`；新增 `AppearanceGeometryCandidate(Mutex<Option<PreparedGeometry>>)`；穩定 source owner worker 新 match arm；新增 `AppearanceContext::prepare_geometry` channel 方法。

**沒有完成新的 extern C API，也沒有編譯以上變更。** 不應假設已可從 UE 呼叫。請繼續新增 API 和測試，再建置獨立測試執行檔。

下一個版本建議用 R119，保留既有 R117 immutable test binary。

## 七、建議接線順序與風險

### 1. 完成 additive geometry C ABI

檔案：`Tools/ok-extract/src/appearance/live_head_ffi.rs`。

- prepare/free/info/card/checkpoint/validate+commit/stage-session clone。
- 沿用原 appearance Context 的 stable worker，不能改成任意 UE pool 直接碰 ParsedBundle RefCell caches。
- opaque geometry candidate 獨立，頭部既有 ABI 保持相容；checkpoint 可共用。
- stage 回傳獨立 owned OkSession，不能把 candidate 內借用指標交給 UE destructor。
- 既有 `ffi::ok_session_clone_interaction` 可參考 preview bytes roundtrip；clone 保留 textures/private accessory runtime，會失去 source_property，僅供 UE staging，native commit 必須用原 candidate。
- stage clone 的 global rig／reference export 與新零件材質要測 parity，不能只檢查 count。
- 測 null/invalid/small buffer、validation 不消耗、stale 拒絕、不改 live、失敗仍可釋放、checkpoint 離開 Context 後有效。
- `commit_geometry` 接收 owned candidate；對失敗保留 candidate 的 C ABI 契約要仔細處理，不能先 take 再發現可預見 validation 錯誤。

### 2. C++ owned wrapper

- `Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKRustExtract.h`
- `Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKRustExtract.cpp`（appearance extern C 約1017行）

可在 FOkAppearanceCandidate 加 geometry discriminant、分派 info/card/destructor/checkpoint/commit；或新增明確型別。FOkSession private Handle 目前只 friend FOkAppearanceContext，可透過 Context 採用 stage clone owned pointer，設 portable true/shared animation false。先不要讓不完整 geometry 進 UI supported list。

### 3. UE unregistered staging + 原子發布

- `KKPreviewActor.cpp` 的 `BuildSessionPartRange(Source,FPartBuildOutput&,Begin,End,Error)` 約3467行已抽出，可擴充 staging mode。
- 新元件準備期間不 RegisterComponent，避免 temporary visible/tick 或 render state 抖動；不要暫時替換 actor.Session，也不要建 full child preview actor／額外32MB shadow target。
- 所有 candidate components 用 `MakeUniqueObjectName` 或唯一 prefix；現有 Skel_/Mouth_/SkinOutline_ 等固定名字可能覆用 live UObject。
- staging 不可 reuse live BodyMesh/HeadMesh；共享既有 global skeleton 需經 native rig validation。
- staging 跨幀存活必須 TStrongObjectPtr 或可追蹤 UObject owner。只放非 UPROPERTY 的 TObjectPtr array 不足以 GC 保護。
- builder 現有多處 AddToRoot：新增資源要清楚區分擁有權，rollback 只處理 candidate；不能 RemoveFromRoot 舊的／shared texture。
- attachment 暫不修改 live tree，待 commit 一起發布。Register 後重新套 pose/morph，避免 OnRegister 重設。
- validation 全完再 native commit，隨後 UE 無失敗發布；只 destroy 移除槽位元件。
- retained／new SessionIndex、component tags KKSessionPart_i、mouth references、followers、morph/upload scratch 必須一致 remap，新增 index 使用 added_range.start offset。
- `Source/OpenKoikatsu/OKGalleryAppearance.cpp` 現在支持11頭部目標，成功完成此路徑才加入 accessory target。

### 4. Legacy APNG runtime context/provenance upgrade

C001／舊卡不能假設有 accessory context/provenance。不得用 mesh name 猜 slot，也不能永久沿用全卡重建作為聲稱的快路徑。

- filename-free source helper 能還原精確來源；可一次升級整組 serialized accessory roles，保留 body/head/hair/clothes，之後 exact-slot delta。
- 來源 context 和 APNG global rig 必須經 shared display normalization + parity 證明。
- 完整 source pipeline 每 outfit 約20秒，目前不可當作 <300ms click 實作；需找出 context-only 必要依賴、減少初始化工作，或在明確初始化階段建立。
- `lib.rs` 約9050行建立 accessory context，可調查 typed context-only extraction 早退，不能靠 sentinel error／temp PNG。
- 全 source pipeline 會寫 process-global EYES_OPEN_MAX_BITS/EYEBROW_OPEN_MAX_BITS（約7480行）。背景並行萃取前需隔離這些 policy 或獨占初始化，不可悄悄影響 live evaluator。

### 5. 其餘類型與完整驗收

剩餘289個 target 仍有 full geometry/source/file transaction，不能認為全都已快。依材質／skin/hair/clothing 類型建立對應局部更新，保留來源／保存一致性，再跑真正 registry-order3000。

## 八、測試現況與已知失敗

- R115 installed integration：`appearance::single_slot_tests::single_slot_card_patch_preserves_other_renderers_and_outfits`，1 passed，42.28s。含 context prepare/no-write、commit、stale、checkpoint、其他六 outfit、reset、native retained pointer／clock parity。
- R115 九組回歸全部 passed；appearance regular27，其餘每組有最低 test count 和 binary hash guard。
- R117：`portable::tests::runtime_apng_parameters_survive_all_outfit_loads`，1 passed／0 failed，4.37s，C001 七 outfit。
- 新 R117 測試檢查 editable shapes／ABMX policy、重複 refresh、finite vertices/normals、triangles/indices、private runtime穩定、reference part count 和來源 SHA256。
- **歷史 reader all100卡整合沒有通過**：第一次 filter 用 `portable::tests::reader_all_outfits_integrity`，實際 zero tests。第二次正確 `portable::reader_regressions::reader_all_outfits_integrity` 缺 `output/CrowdAPNG/C000.png` fixture，立即失敗。不要將兩次視為成功，不能造假舊 receipts 或複製卡湊100張。
- 保留失敗證據 `native-portable-integrity-r115.log`（zero tests）、`native-portable-integrity-r115b.log`（missing fixture），原固定 report 若存在已備份 `reader-integrity-before-r115.json`。
- R117 七 outfit 真實卡測試是獨立補充，並不取代歷史100卡 oracle或3000 UI驗收。
- formal snapshot export 仍約600ms（R115 639.838ms）；這是 deferred save，不是 GUI選擇已達300ms的證據。

## 九、可用執行命令

PowerShell，先確保沒有正在跑的 GUI效能量測，再做 heavy compile/test；不同輪使用新 Tag／新 test exe，保留失敗。

### Native compile

在 `C:/portable/OpenKoikatsu/Tools/ok-extract`：

```powershell
cargo test --release --lib --no-run --message-format=json > ../../Saved/Tests/AppearanceSelectors20261010/native-build-r119.jsonl 2> ../../Saved/Tests/AppearanceSelectors20261010/native-build-r119.log
```

從 JSON `compiler-artifact.profile.test=true` 取得 executable，複製到 `test-binaries/appearance-r119.exe` 後再跑。編譯通常70秒；錯誤細節可能在 JSON `.message.rendered`，不是 stderr。

```powershell
& C:/portable/OpenKoikatsu/Saved/Tests/AppearanceSelectors20261010/test-binaries/appearance-r119.exe appearance::single_slot_tests::single_slot_card_patch_preserves_other_renderers_and_outfits --exact --ignored --nocapture --test-threads=1
& C:/portable/OpenKoikatsu/Saved/Tests/AppearanceSelectors20261010/test-binaries/appearance-r119.exe portable::tests::runtime_apng_parameters_survive_all_outfit_loads --exact --ignored --nocapture --test-threads=1
& C:/portable/OpenKoikatsu/Tools/run_appearance_native_regressions.ps1 -Tag NativeR119 -Executable C:/portable/OpenKoikatsu/Saved/Tests/AppearanceSelectors20261010/test-binaries/appearance-r119.exe
```

每次檢查 exit code 和明確非零 test count。PowerShell script 用 throw／summary 判定，不要誤讀 stale LASTEXITCODE。

### UE module-only build

```powershell
& D:/UE5.8/Engine/Build/BatchFiles/Build.bat OpenKoikatsuEditor Win64 Development -Project=C:/portable/OpenKoikatsu/OpenKoikatsu.uproject -NoEngineChanges -NoHotReloadFromIDE -WaitMutex -MaxParallelActions=8 -Module=KKCard -Module=KKUnityBundle -Module=OpenKoikatsu
```

header 修改會擴大建置工作，需耐心等完成。不要改 engine。

### 實際 UI 診斷子集

```powershell
$env:OK_APPEARANCE_SWEEP_START='2'
$env:OK_APPEARANCE_SWEEP_LIMIT='70'
Remove-Item Env:OK_APPEARANCE_PROFILE -ErrorAction SilentlyContinue
& C:/portable/OpenKoikatsu/Tools/run_editor_acceptance.ps1 -Tag FreshUniqueTag -Card Content/CharacterCards/CrowdAPNG/C001.png -Mode AppearanceSweep
```

LeftEye start23/limit20；RightEye start27/limit20。完整驗收移除兩個 SWEEP env，需新 tag。runner 有 DLL／manifest hash guards、實際 UI callback/render fence／saved oracle、1600×1000、owned process cleanup。

未完成 geometry 前不要再啟動已知會大量 full-rebuild 的長時間3000 baseline，先完成快路徑並用有針對性的實際 UI測試排錯。

## 十、接手即刻清單

- [ ] 讀本文件、計畫／錯誤 MD；檢查 git diff，保留不相關修改。
- [ ] 完成最新未編譯 geometry FFI 骨架及生命週期／原子性測試，使用 R119 新 exe。
- [ ] 完成 C++ ownership wrapper、UE unregistered staging 和一致 remap，先驗真實畫面再啟用 UI支持。
- [ ] 實作 legacy APNG 初始化升級與 source policy 隔離，C001 必須可用。
- [ ] 為其他材料／幾何類型補齊通用局部更新。
- [ ] 實際 registry 順序每target十次、共3000次，全顯示／保存／<300ms；保留失敗報告。
- [ ] 完成後才作整體通過結論；子集與 native pass 不代替完整驗收。

交接時沒有正在執行的 native compile/test。本輪已收到 session92499 成功完成；舊 session50016 是已完成的歷史回歸／錯誤zero-test filter，無需重新等它。最新工作只寫了未編譯 FFI 骨架，沒有啟動新建置或部署。

## 十一、2026-10-10 後續進度（R119–R132）

**仍未完成整體驗收。** 本輪完成項：

- [x] geometry C ABI（prepare/info/card/stage/checkpoint/commit/free）+ 生命週期測試；commit 先完整借用驗證才取用，可預見失敗保留 candidate。
- [x] HashMap/HashSet 序列化改排序（`ordered.rs`），content-addressed ID 決定性。
- [x] C++ `FOkAppearanceGeometryCandidate` 與 Context 方法；`BuildSessionPartRange` stage mode；`AKKPreviewActor::ApplyAccessoryGeometryCandidate`；UI `ApplyPreparedGeometry`（`OK_APPEARANCE_GEOMETRY=1` gate）。
- [x] thread-scoped export profile、references 變更通知、shaped-globals template、bundle source memo、cached accessory_color_use；各有 parity／單元測試。
- [x] 實際 UI：accessory1_124 十項 10/10 幾何路徑顯示／保存正確，平均 228.6ms，8/10<300ms（`Saved/Tests/EditorAcceptance/AppearanceSweep20261010AccessoryGeometryR132`）。
- [ ] 首次點擊（384.7ms）與尖峰（310.9ms）。
- [ ] C001 legacy provenance：只能初始化階段整組替換 accessory（R123）。
- [ ] 其餘 target、3000 次完整驗收。

注意：
- 另一個 session 同時在此 repo 工作並自動 commit 工作樹；`target/release` 測試 exe 可能被其占用。本工作 native 測試使用 `CARGO_TARGET_DIR=Tools/ok-extract/target-appearance`（已加入 .gitignore），測試 exe 依序存為 `test-binaries/appearance-r1xx.exe`。
- 加速診斷可用 native benchmark：`OK_GEOMETRY_BENCH_CARD=<fixture> appearance-r132.exe appearance::live_head::tests::geometry_preparation_benchmark --exact --ignored --nocapture`。
- R132 native：single-slot+geometry C ABI+template parity、cached color-use parity（80 項）、benchmark 通過；NativeR132 九組回歸全通過（appearance 27）。
