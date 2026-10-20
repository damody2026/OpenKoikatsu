# 資源管線

原作 Unity 2019.4.9 AssetBundle 活在使用者的 `GameRoot`：

- `abdata/list/characustom/*.unity3d`：`【ChaListData】` 列表。欄位包含 `ID`、`Name`、`MainAB`、`MainData`。
- `abdata/chara/*.unity3d`：身體／頭／髮／衣服 mesh 與貼圖。
- `mods/*.zipmod`：內部同樣是 `abdata/` + `manifest.xml` `<guid>`。

開源 repo **不准提交** 這些檔。

3D 預覽目前走：

1. C++ `FOkSession` 透過 `ok_register_bindings`（unreal-rust 風格函式指標表）呼叫 Rust in-memory session：rest mesh / weights / PNG 留在 Rust heap，不寫 OBJ。滑桿走 `apply_shape` + CPU `skin` 再更新 `UProceduralMeshComponent`。
2. 失敗時 fallback：C++ `OKExtractCard` 寫 `Saved/Converted/*.obj`，`AKKPreviewActor::LoadDirectory` 讀進來。CLI `ok-extract` 仍寫磁碟。

目前每個 GUID 取最大 mesh，還不是 ChaListData 槽位對應。完整 runtime UnityFS 解析仍待補；這條轉換路徑先讓 viewer 看得到原作 mesh，而不把 Illusion 資源打進 git。
