# StreetSocial 測試用資料

C++ 自動化測試（`OKStreetSocialRules.h` 的 `OKStreetCheckIntegration`、`Tests/OKStreetSocialRules.spec.cpp`）讀的固定輸入。

- 這些是 2026-10-07 以 `Tools/prepare_street_social_selection.py` 產生的 selection 與 dialogue。
- 角色卡：男主 Male01，女主 C000、C019、C044、C022、C068（性格 0、18、17、27、37）。
- 原本放在 `Saved/StreetSocial/`；因為 Saved 隨時可能被清掉，所以搬到這裡納入版本控制。
- `selection.json` 裡的 `dialogue_catalogue` 已改指向本資料夾。`save_path`、`report` 是執行時的輸出位置，仍在 Saved，測試不會讀它們。
- 現在的工具和型錄都已經改版，重新產生不會逐位元組相同，所以不要用新工具覆蓋；要改測試期待值時，再一起換新資料。
- `CompletionData/catalogue.json`：schema 2 的完整型錄（1230 節點），給 `OKStreetCheckRelationship` 檢查 branch 物件；原本也在 Saved。
