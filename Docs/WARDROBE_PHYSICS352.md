# Stage352：一般衣服次級物理縮放

範圍為穿衣角色的一般走路與非場景骨鏈控制，不輸出圖片。產品版本仍為 SESSION240 / CLI350 / CACHE108；新增內容均為 cfg(test) 或驗證文件。

原生依據為 `C:/portable/KoikatuSunshine/kks_decomp/Managed/CharaStudio_Data/Assembly-CSharp/DynamicBone.cs` 的 UpdateDynamicBones / UpdateParticles2：每幀讀取 component lossyScale.x，使用當幀 Transform 距離決定鏈長，先碰撞再投影回鏈長。故最終粒子不一定完全在碰撞器外，不能加一個反於來源順序的「完全無穿插」斷言。

通用控制維持同一 AttachedDynamicSet 歷史，倍率 1 / 0.6 / 1.8 / 1，每段60幀；比較碰撞開啟與關閉，確認當幀鏈長、有限值、另一 instance 不變。實卡控制使用 KK_226176、KK_224305、Alice_keiyuu 與 March_7th，一般走路各240幀，統計實際共享及私有衣服粒子，不將零粒子案例當物理正控制。

通用骨鏈控制 UnitR2 PASS：480次觀測符合當幀鏈長（誤差門檻2µm）、粒子位置有限、另一instance不變；碰撞開關的最小歸一化末端位移0.185218945m。UnitR1球心恰在正軸，碰撞後鏈長投影回原位，位移0導致正控制失敗；改側偏0.1m，保留負紀錄，沒有產品修正或放寬門檻。

InstalledR1 PASS / 250.02秒 / actualexit0：4卡 × 4倍率 × 60幀 = 960幀。KK_226176與KK_224305的衣服粒子皆0，僅作靜態衣服有限值/隔離控制。Alice與March_7th各48共享衣服粒子，480物理幀共23,040粒子位置觀測；每段59/59轉換皆變化，nonfinite0，其他session骨架/頂點不變。期末粒子觀測384不是逐幀總量；私有衣服粒子0，不宣稱實卡私有物理覆蓋。

dbone suite 28PASS / 0FAIL / 11ignored / 0.71秒 / actualexit0，包含既有私有槽物理、implanted碰撞、cache和角色隔離控制。此次只加入測試，產品LIB/DLL不需重建，沒有新增卡片、時間點或動作補丁。日誌及數值報告位於 `Saved/Tests/WardrobePhysics352` 與 `Saved/Logs/WardrobePhysics352*.log`；Audit.json保存本輪source、native decompile、四卡、test EXE、現有LIB/DLL及結果SHA。

未驗收原生同輸入粒子逐幀 parity、衣服網格穿插、配對場景位置或全面無 bug。碰撞反應與有限值不是衣服完全無穿插證明；不重新封存或覆寫Stage350/351歷史報告。
