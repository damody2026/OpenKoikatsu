# 實際模型製作佇列

來源SHA：`fdb35f1e1c44491ef9131fd24ff9cf30f4f1c37036d01037d068dacc95b4cbb0`。

語意總成20,538筆，展開後需要8,794種獨立模型；真正通過檔案與證據驗收1種。
1,280個主要室內單元中，可進入完整正式組裝者0間。未解決的外牆、扶手及電梯細節另保留，不把控制幾何當生成模型。

書桌等共用零件只生成一次，仍依每房原點及旋轉放置；房門與設備未具備相同用途／功能契約時，不只憑相同尺寸合併。

## 已驗證模型

- COMMON-CITY-study_desk

## 下一批依重用範圍排序

| 資產 | 尺寸m | 依賴此件的房間／建築數 |
|---|---|---:|
|MOD-e0279e85c39f0a73|[0.2, 0.2, 3.6]|526|
|MOD-54fcbfe42ee21d34|[0.2, 5.0, 3.6]|467|
|MOD-64a64c2a80d47bbb|[1.2, 0.2, 1.2]|462|
|COMMON-WC-toilet|[0.75, 0.75, 0.85]|349|
|COMMON-CITY-basin|[1.2, 0.6, 0.85]|338|
|COMMON-CITY-shower|[1.5, 1.5, 2.3]|333|
|MOD-9f428016399d75f0|[0.2, 0.2, 3.2]|297|
|MOD-f4f9409a5cc01d62|[1.2, 0.2, 0.8]|297|
|MOD-0b3f2bcdf7acc5a5|[0.9, 0.2, 3.2]|290|
|COMMON-CITY-seat|[0.6, 0.6, 0.9]|278|
|COMMON-CITY-workstation|[1.8, 1.8, 1.05]|239|
|MOD-54d00e5cb1b8e612|[1.2, 0.2, 1.0]|223|
|COMMON-CITY-console|[1.2, 0.6, 1.3]|222|
|MOD-7c9f1bdd38457ee7|[0.9, 0.2, 3.6]|221|
|COMMON-CITY-bunk|[1.1, 2.1, 1.9]|220|
|COMMON-CITY-campus_wardrobe|[0.65, 0.6, 1.9]|220|
|MOD-f5fdc9eca7ec8c3c|[4.755667, 0.2, 3.6]|219|
|COMMON-CITY-checkout|[2.4, 0.8, 1.1]|215|
|MOD-26375544d885824a|[0.2, 3.0, 3.2]|215|
|MOD-cf19fd0b838e4d13|[0.2, 4.5, 3.2]|212|
|MOD-887910ae10d92e92|[0.900167, 0.2, 3.6]|211|
|MOD-404a4d9eb1f1c852|[4.055611, 0.2, 3.6]|211|
|MOD-58f63f1907849fc0|[3.2, 0.2, 3.2]|208|
|MOD-77154807f8a1c7f2|[3.8, 0.2, 3.2]|208|
|MOD-6cc310249dabeadd|[4.0, 0.2, 3.2]|208|
|MOD-98a2ffc20f40427d|[5.9, 0.2, 3.2]|208|
|COMMON-CITY-lectern|[1.6, 0.8, 1.05]|167|
|MOD-223d7691ca194a61|[0.2, 0.2, 3.8]|151|
|MOD-d4363761361ed56d|[0.2, 1.6, 3.6]|149|
|MOD-2d9c54f1157b2880|[0.2, 4.4, 3.6]|149|

## 優先恢復既有生成，不重複扣點

以下項目已有相同幾何版本的 Tripo 模型，應從既有頁面恢復匯出；僅下載並驗收成功後才可組裝。

- [COMMON-CITY-seat](https://studio.tripo3d.ai/workspace/generate/beae71da-dbbf-4b21-9706-8117f9a31f58)：Existing chair is visible in signed-in Tripo. Current STL semantic/native and GLB export attempts received no download event or file; cause unconfirmed. Resume this model export, do not regenerate or approve.
