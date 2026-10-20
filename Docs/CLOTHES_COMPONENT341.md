# Stage341：服裝 component 與共享 RT

本次修正共用匯入流程，不依卡名、圖片 ROI 或 outfit 編號分支。

## 原生依據

- `kks_decomp/Managed/CharaStudio_Data/Assembly-CSharp/ChaControl.cs` 的 `ChangeCustomClothes`：RT0 或實際載入 prefab 的 `ChaClothesComponent` 缺失時直接返回；只改 renderer 的第一個 material。
- `kks_decomp/plugins/KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayController.cs` 的 `GetApplicableRenderers`：依序選 normal01/normal02，否則 alpha01/alpha02 的第零個 renderer，不能改成搜尋第一個非空元素。
- 同檔 `ApplyOverlays`：依序取得所選 renderer 當下 `mainTexture`；不是 RenderTexture 時停止；直接修改共享 RT。同一 RT 被選到兩次時也處理兩次。

## 修正

1. 不再把 bundle 中任意其他 prefab 的 component 當作 fallback；必須屬於要求的 prefab 子樹。
2. component 缺失或 RT0 缺失時，不寫入服裝 create 貼圖，保留 prefab 原材質。
3. overlay 保存原生選取 renderer 的順序，根據實際可用 RT 重播指派結果，再更新共享同一 RT 的所有匯出副本。較後面的 RT 缺失時，保留較早成功指派的 RT。
4. 所選 renderer 沒有匯出 mesh 時，其共享 RT 仍可透過其他 renderer 的副本更新。非 RT 則停止，不能繼續處理第二個 renderer。
5. 最終 SESSION_PIPELINE_VER=221、CLI cache=331。220 是本次中途版，不能拿來驗收最終修改。

## 驗證與界線

驗收結果將以 `Saved/Tests/ClothesComponent341/Acceptance.json` 記錄。原始 dump 中的 missing/different 必須保留，不能把測試執行成功當作所有 dump 相符。

中途編譯出現 Windows LNK1104：同一 release 測試 EXE 正在執行六套匯出時，另一個 cargo test 嘗試重新連結它。必須等執行結束再重編與執行最終測試，不能把失敗編譯算作 PASS。一次 PowerShell 路徑操作誤把 repo-relative 路徑拼到 Tools/ok-extract cwd；檔案不存在，沒有寫入成功，已改用明確絕對路徑／apply_patch。

## 最終結果

- 服裝回歸 22 PASS；runtime texture 回歸 4 PASS；三張男卡 prefab isolation 1 PASS（每張包含有效 prefab 與不存在 prefab 的反例）。
- 最終 UE build Succeeded，80.96 秒；六套匯出正常結束，279.53 秒。
- 六套 176 parts、120 effective inputs PASS，與 Stage340 匯出比較：所有貼圖 0 changes、part identity 相同。這是保留有效卡片行為的回歸證據，不是宣稱 Stage340 已等同原生所有像素。
- 菲比、KK_224305、ill_male_01、ill_male_02：4 卡 × 4 視角，共 16 張 900×1080；runner material/GPU/view/shadow readiness 通過、exit0，所有影像已目視檢查並有非灰屏內容與 hash gate。
- 原始 dump 核對仍是 62 matched_inputs／118 different／117 missing；all_dumps_match=false。未把未匯入的原生非啟用/state renderer 刪除，也未把跨狀態材料差異調成圖片特例。
- 可重跑驗收：`python Tools/audit_clothes_component341.py`。`Saved/Tests/ClothesComponent341/Acceptance.json` 保存範圍、source/staticlib/DLL hash、日誌、six-outfit comparisons、16 captures；同名輸出前須自行保存歷史。
- 本次 owned UE/cargo/native 程序已結束；未修改原卡、APNG 或 Gallery。
