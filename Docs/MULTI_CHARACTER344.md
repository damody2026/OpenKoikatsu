# Stage344：菲比多人物載入與 dump 身分邊界

需求：`C:/portable/KoikatuSunshine/mesh_dump/菲比v1.1.png` 的多人物讀取須正確；採共通角色所有權規則，不新增卡名、顏色、骨架或 outfit 特例。

## 計畫與實作

1. 檢查每人物的 session、renderer、貼圖與生命週期；保留獨立 portable/full-head 修正。
2. 同時開啟菲比 outfit 0、1、2、3、5、6、三張男卡及重複菲比卡，共 10 個實例；用正反載入順序、逐人修改形狀／姿態、刪除及重開檢查互不污染。
3. dump 先通過完整人物 root 邊界，再接受 mesh/material 候選；未知 root 及同名他人資料不能取得 matched 身分。
4. 封存來源 SHA、編譯／測試紀錄；保留 raw 差異及原卡身分證据限制，不宣稱全面零 bug。

## 共通修正

`AKKPreviewActor` 原先僅於重新載入呼叫 `DestroyAllParts()`；`LoadedTextures` 曾 AddToRoot，銷毀 actor 未 RemoveFromRoot。加入 `EndPlay()` 與 `Destroyed()`，呼叫既有可重複清理並關閉本人的 session，涵蓋尚未 BeginPlay 的 editor actor。原生 `kks_decomp/Assembly-CSharp/ChaControl.cs:484` 的 `OnDestroy` 呼叫 `ReleaseAll`，後者釋放本人的 load/face/body/coordinate/custom/hand/info controllers；同樣以人物所有權為單位。沒有改持久化 cache/schema 或全域卡片資源。

`audit_outfit_dumps.py` 新增人物邊界：`hierarchyPath` 必須等於 `character_source.gameObject` 或位於其 `/` 子路徑；有 `sourcePart.objectHierarchyPath` 時也須符合。可移除原生 `CommonSpace/` 前綴，不能用近似字串或 mesh 名猜另一人物。未知來源標 unverified_character，外人標 foreign_character。新增 `--output`，不覆蓋先前封存的 Stage343 報告。

## 驗證結果

- `Saved/Tests/MultiCharacter344/DumpIdentity.json`：六套、282 renderer，全部通過 root 邊界；282 個保留同 mesh/material/vertex 的他人路徑負控制被拒絕，未知 root 也拒絕。
- `Saved/Tests/MultiCharacter344/DumpAudit.json`：root guard 後仍 64 matched_inputs／116 different／117 missing，`all_dumps_match=false`；176 runtime parts 全有對應。沒有用隔離通過消除原始 raw 差異。
- 第一輪 Unreal 兩項測試通過，10 個 actor 在同一 world：所有貼圖指標獨立、component owner 正確，逐人改身材／臉形不影響其他人，刪除本人的貼圖 unroot 而其他人仍可用。
- 最終 world context 修正後 Unreal build Succeeded（6.30 秒），兩項測試皆 Success，queue empty／exit 0；無 DestroyActor world context 警告，StartBinary.json 與 EndBinary.json 一致。
- Rust 正反序測試 PASS（2331.51 秒），每個順序同時持有 10 sessions、逐人共 20 次形狀／骨骼狀態修改、刪除後 survivor 比對及重開原始形狀均通過。`RustIsolation.json` 記錄明確卡片 SHA；測試使用當前 full-head/portable source 版本。
- `Acceptance.json` 的 bounded_ownership_passed=true：兩項 Unreal／Rust 正反序／282 renderer 外人負控制／原始檔案 SHA／DLL 起訖一致全部核對通過。原卡 bytes provenance 與全面 native pixel parity 仍標 false。驗收工具 exit 0；所有本輪自有程序已結束，UE／Rust test EXE 窗口釋放。

## 原始 dump 的身分限制

六套 `card_resolver.json` 有最多七個候選，strategy 包含 `nameIndex+shape`／`nameIndex(first)`，沒有原始載入卡片 bytes SHA。因此現有 dump 可證明人物 root 所有權，但不能證明 resolver 猜中的檔案必是原始卡。測試使用明確卡片路徑及 SHA，兩者不可混為一談。這次沒有修改原始卡與 dump，也没有將卡名相似或來源缺項宣稱全像素符合。

## 驗證工具錯誤

首次 UE build 使用不存在的 `Components/ProceduralMeshComponent.h`，改為模組標頭 `ProceduralMeshComponent.h` 後編譯成功。首次 test 在 `CreateWorld` 已初始化後再 `InitializeNewWorld`，觸發 LevelActor.cpp world settings fatal；改將 InitializationValues 傳入 CreateWorld，不能重複初始化。Automation 的 ExecCmds 不附 `Quit`，使用 TestExit queue empty 等完成。第一輪測試 world 無 engine context 有 DestroyActor warning；最終 fixture 建立／移除 FWorldContext。以上失敗保留原 log，不當作 production 通過證據。

## 重跑

Rust ignored 測試必須明確設定 OK_MULTI_GAME、OK_MULTI_CARD、OK_MULTI_REPORT，再執行 `cargo test --manifest-path Tools/ok-extract/Cargo.toml --lib real_card_multi_character_isolation -- --ignored --nocapture`。未安裝卡片或未設定環境變數會失敗，不會靜默略過。

Unreal 使用相同明確卡片環境、OK_GPU_SKIN／OK_SKELMESH／OK_NOPOSE=1，Automation RunTests OpenKoikatsu.Import. 加 TestExit Automation Test Queue Empty。結束後再執行 `python -X utf8 Tools/audit_multi_character_acceptance344.py`，它重新核對卡片／dump／測試二進位及來源 SHA、測試成功數、負控制及保留的 raw 差異。
