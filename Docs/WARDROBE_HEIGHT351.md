# Stage351：一般站姿／走路的衣服身高與可達範圍控制

本節驗證通用幾何路徑，沒有修改卡片，也沒有用圖像補丁。產品仍為 SESSION240／CLI350／CACHE108；新增測試工具與文件。

## 骨架與衣服

反編譯 ChaControl.LoadClothes 的 copyWeights=1 呼叫 AssignedWeightsAndSetBounds，替換 renderer.bones 而保留 mesh 的 bindposes。私有 SMR 槽位及未匹配骨骼不能被粗略合併。ABMX BoneModifier.Apply 使用 baseline.localScale × ScaleModifier；這次以相同 runtime ABMX 路徑對 cf_n_height 加入暫時倍率，原卡及其他modifier保持不變，不再直接改 shaped tree 冒充ABMX。

KK_226176女卡、KK_224305男卡，outfit0；native身高0/1 × 倍率1/0.6/1.8 × 靜止站姿及走路0.4/0.8/1.2秒，共48角色幀。來源controller state分別是 `walk_run / Base Layer.f_aruki_00_00_01`、`male / Base Layer.mc_m_walk_00_00_01`。站姿關閉Animator取shaped骨架；全部測試關閉次級物理，以隔離幾何縮放，不能當物理/衣服碰撞測試。

每幀女卡6件／6760頂點、男卡5件／9121頂點，合计381144個衣服頂點觀測，全數active權重槽位可匹配到height節點下的body骨骼，無排除部件。最大均勻縮放誤差1.1935821e-6m（0.001194mm），無非有限值；另一個同卡session的骨架/頂點snapshot不變。身體CPU實際蒙皮與GPU共享參考骨架最大差6.1795363e-5m（0.061795mm），原0.2mm界線通過。

全部11衣服部件都使用保留私有SMR槽位的runtime路徑，實際輸出頂點有驗證，但沒有共享GPU衣服palette正控制。不能因身體GPU通過而宣稱私有衣服GPU也通過。詳 `Saved/Tests/WardrobeHeight351/R4/report.json`，1 PASS／20.01秒。R1/R2/R3失敗保留，並非產品修正前後的Red控制；錯誤E229說明無效清單/覆蓋假設。

## 一般手部可達範圍

利用Stage350 NativeR3的無卡片、無動作、停用renderer原生骨架伸展資料，按當幀兩段臂長與求解後肩部位置計算目標距離下界。96個目標中66個超過該固定肩部位置的可達範圍。最大原生residual0.1478970009m，幾何下界0.1478909484m。這是**給定求解後根位置**的下界；肩部/身體根若移動可改變可達範圍，不能宣稱完整身體/場景永遠不可能達成。

資料在 `NativeReachBounds.json`，工具 `Tools/audit_ik_reach351.py`。本結果支持該一般伸展控制的主要大殘差來自可達範圍，不構成Stage349場景極端身高相同輸入的原生oracle。

## 顯示端與限制

Unreal增加 `OpenKoikatsu.Import.ClothedActorScaleIsolation`，兩個實際衣服actor、一般走路三個時間點，world scale0.6/1.8/1回復；逐個衣服元件量測world bounds是否按actor原點縮放、另一actor pose/materials/world bones是否不變。CPU/GPU各自斷言實際body renderer，不能靠環境旗標冒充路徑驗證。最終結果以 `Audit.json` 與實際程序退出碼為準。

均勻縮放與有限值不等於衣服沒有穿插。此節不認證次級物理、body/garment碰撞、不同世界transform的配對播放、場景的互動對位或全面零bug；既有Start世界transform guard保留。

最終：UE build R2 Succeeded7.26秒；CPU/GPU皆ClothedActorScaleIsolation Success、actualexit0。每個模式99個衣服元件world bounds觀測，最大origin/extent誤差0cm，另一actor的Session pose/材料payload及world bone snapshots不變；body renderer選擇斷言通過。這是一般顯示元件的獨立縮放，沒有呼叫paired playback。`Audit.json`封存32項source/native decompile/cards/fixture/報告/日誌/LIB/EXE/DLL SHA。所有自有Rust/UE程序結束，未安裝native probe、未產生圖像；本輪未發現需要修改產品衣服縮放邏輯的問題，沒有按卡片補丁。
