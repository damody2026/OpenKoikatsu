# 方案2／1024 runtime液體atlas實作計畫

1. 預設AtlasResolution=1024，代表共用page大小和全場景一致的texel/cm密度；依每face原始捕獲面積配置4..64的pow2 chart，不按性別／mesh拆分數分配解析度。配置固定至clear／部件替換，不隨動畫每幀重配。
2. 保守transport支援相鄰face不同解析度：來源質量用來源格面積、目的插值用目的格面積，維持原double質量ledger。舊uniform Size測試相容。
3. runtime共用atlas page allocator，男女及各部件共用1024 pages，按首次命中／液體擴散lazy配置chart。區域上傳，同page不同face不得互相清除；材質與overlay按page綁定，多page可擴充。
4. 清除／移除部件回收配置，避免反覆重進／替換漏掉pages。所有資料runtime建立，APNG沒有特化資源。原高模、物理、120Hz彈道／20Hz表面更新保留。
5. 測試mixed-resolution跨face守恆／brush／drip，atlas混合尺寸不重疊、回收再用；既有Film／Scene／MaskParity／Transport／ChartCoordinates回歸；真實APNG双人private GPU水槍圖像驗收及無trace profile。
6. 本輪只做方案2，不加入稀疏格點算法，不以降低SurfaceHz或丟棄backlog當優化。錯誤記入INTERACTION_SHOWCASE_ERRORS.md，保留原profile作對照。

## 實作狀態
步驟1..6已完成；六項回歸與真實APNG雙人功能PASS，獨立無trace profile完成、四DLL核對一致。方案二／1024通過；噴射120fps仍未達標，實際75.29fps男角／94.91fps女角。錯誤與修復已追加到INTERACTION_SHOWCASE_ERRORS.md。完整設計／驗收見 [INTERACTION_WATERGUN_ATLAS_1024_ACCEPTANCE.md](INTERACTION_WATERGUN_ATLAS_1024_ACCEPTANCE.md)。
