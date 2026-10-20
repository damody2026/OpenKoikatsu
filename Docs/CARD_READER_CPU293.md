# 讀卡 CPU 模式與載入加速（E293，2026-10-05）

使用者本輪範圍：CPU 預覽正確；水槍可見、透明度可調、衣服染色及向下流痕；真正半脫／全脫及穿回；APNG 載入低於十秒。GPU 五官調查停止，未修的其他視覺問題保留 ERROR_LOG。

## 使用

- 執行專案根目錄 CardReaderDevelopment.cmd。預設 CPU 蒙皮，直接開 output/CrowdAPNG 的 100 張 APNG；可在介面切回 KKS 卡目錄。
- 按住 A 再按住滑鼠左鍵噴射，R 清除。水槍參數面板「水柱不透明度」範圍 0–1，0 全透明、1 不透明；黏度及表面流速另有滑桿。
- 衣服「穿上／半脫／全脫」載入實際模型狀態，穿回可恢復。正式庫已有 100 卡、720 outfit、1440 目標狀態資源，無需再次轉換；狀態範圍為上衣、下裝、胸罩、內褲的 111100000 與 333300000，不是九欄全部組合。

## 共用修正

1. 啟動器明確指定 OK_CPU_SKIN=1，清除 presence-based GPU／NOPOSE 旗標；只改啟動子程序環境，啟動後還原父程序。
2. 讀卡 worker 擁有 session 時，先複製不可變貼圖輸入，再以依 CPU 核心數最多八個 decoder 並行準備 PNG/mip。工作執行緒不建立或修改 UObjects。Cache 的 reference count、LRU 和 bytes accounting 改為 thread-safe；保留 512MiB 上限及完整來源 byte identity，UTexture2D 仍每 actor 獨立。
3. 偶數 2:1 mip 縮小採四個 encoded texel 的整數平均，保留原生向下取整及 alpha=127 邊界；奇數與單列／單行仍採原來 normalized-UV 計算。
4. 新建 RF_Transient 預覽材質標記 bIsPreviewMaterial，避免每個材質觸發全場景 render-state 重建；PostEditChange 已編譯 shader，不再立即重複 ForceRecompileForRendering。Engine 此旗標為 transient/duplicatetransient，不隨永久資產保存或複製。

## 已完成驗證

- Reader293BuildR11 成功。驗收固定使用 1024 最小測試 snapshot；正常專案的後續並行建置另核對 Engine/plugin BuildId 相符。
- CpuTex293R2：PngCache 並行準備、精確 texel、獨立貼圖/sampler、content invalidation、LRU 通過。
- CpuMip293R1：RuntimeTextureMips 與原遊戲 dump 對照通過，含 127.5→127 裁切邊界。
- Reader293CpuFastR2：CPU、3840×2160 實際 GUI 操作 valid=1/exit=0/圖片檢查通過。水槍命中衣服，透明度 .35；5 秒流痕重心下降 8.272231cm，守恆誤差約零。March 穿著 14832 面、半脫 8741 面、全脫衣物 0 面，穿回、另存後離線狀態、未修改卡重用均通過。該次首次 load_seconds 為 C000=3.133155、C001=8.568213；不等於已驗證全庫低於十秒。
- Reader293CpuLibraryR3 已完成正式 100 卡逐張首套服裝的 CPU GUI 測試：valid=1、exit=0，100 張角色截圖有效。預設 1600×1000 視窗，實際載入 dispatch 到 rendered Worn screenshot 最慢 8.649 秒；load_times.json valid=true、expected=100、measured=100。計時包含視圖及 shader readiness，不包含啟動 Unreal；這個結果沒有涵蓋全部 720 outfit 的逐套顯示時間。

另避免預熱 renderer 不使用的 authored slot；線性遮罩/法線在首次 upload 前指定正確 sampler，省去不必要的 resource release/rebuild。Reader293CpuHeavyR1 三張重卡畫面顯示時間皆低於十秒，最慢 9.352 秒；整庫仍以最後報告為準。

保留原始卡、dump、APNG 預覽及資源。這一輪沒有用角色名稱修正、隱藏網格或降低貼圖解析度來取得結果。個別讀卡時間會受硬體與同機工作負載影響，不宣稱任意環境都能滿足十秒。

### E293 最終驗收（2026-10-05）
- Reader293BuildR11 成功；本輪驗收固定使用 snapshot1024；正式專案後續並行建置的 Engine/plugin BuildId 已核對相符。
- Reader293CpuLibraryR3：100卡首套服裝 CPU 真實 GUI 載入全部成功；1600×1000，從載入到畫面顯示最慢8.649秒，load_times.json valid=true。不含Unreal程序啟動。
- Reader293CpuFinalR1：兩卡3840×2160 GUI完整回歸 valid=1、exit=0、截圖有效且已檢視。水柱 opacity=1/.35 可見並命中衣服；每卡 clothing_coatings=1。停止噴射後5秒流痕重心下降7.051736/8.121519cm，質量守恆誤差約零；空中剩餘液滴仍會入射，表面量26.4→29.2並非無故增量。
- March C001 穿著14832面、真實半脫8741面、全脫對應衣物0面；穿回、另存後離線載入、busy callback保護及未修改卡重用均通過。全庫100卡/720outfit/1440目標穿脫快照已由E292準備並驗證，不需再次轉換。
- CardReaderDevelopment.cmd 預設CPU並直接進入output/CrowdAPNG。A+左鍵噴射、R清除；面板水柱不透明度0–1。共用修正未降低貼圖解析度，未用角色名稱特例。
- 本輪依使用者停止GPU視覺調查，保留E293既有未解記錄；不得用本輪驗收宣稱所有其他問題已修復。

### E293 正式版本補驗完成（2026-10-05）
- Reader293NormalLibraryR1 直接執行正式 OpenKoikatsu.uproject / normal DLL，CPU，1600×1000。100卡首套服裝 GUI及角色影像檢查全部通過；process exit0，report valid1，load_times.json valid=true，expected=100/measured=100，最慢8.425秒（包括視圖及shader就緒，不含Unreal啟動）。
- launch.json 記錄DLL SHA256 B788CFAAD7ACE3A32F5B1EEDE8700F78B99636842C3E0889FB89AE2FAC97F7E9，建置BuildId 8efe457f-c803-4fca-96e9-fb6466924b49；驗收結束再次核對DLL hash未改。此結果補足之前snapshot1024與正式DLL版本差異。
- Reader293NormalFinalR1 正式專案兩卡4K完整GUI功能回歸通過：opacity1/.35、水柱命中衣服、clothing_coatings1、流動5秒下落7.031827/8.272231cm、守恆誤差約0、真實半脫/全脫/穿回、另存離線、busy callback與未修改卡重用。C001_Flow5s.png 已人工檢視。
- 通用測試工具新增ProjectPath（預設保持原snapshot host）、與正式啟動器相同GameMode，啟動前核對Engine BuildId並保存launch.json。未新增角色特例、未改寫有效APNG、未降低貼圖或幾何品質。
- 本輪CPU/水槍/衣服/100卡十秒載入範圍完成；全720 outfit仍為之前資料/幾何完整性驗證，不把100個首套服裝計時當720套性能結果。

### E293 使用者要求最後驗收（2026-10-05）
- Reader293AcceptanceFinalR1 直接正式uproject/CPU，兩卡4K全功能回歸完成：valid=1、exit0、image_checks valid=true，C001_Flow5s圖已檢視。水柱opacity1/.35、衣物coating、5秒流動下落6.997392/8.301426cm、質量守恆、真實半脫/全脫/穿回、另存離線與busy/reuse全部通過。
- 正式DLL SHA256與Reader293NormalLibraryR1及本次launch.json相同，驗收前後未改；因此同版本100卡首套服裝最慢8.425秒的完整GUI驗證仍適用。不把720outfit資料完整性視為720套性能計時。
- CardReaderDevelopment啟動設定核對CPU/output/CrowdAPNG，既有100卡/720outfit/1440目標狀態完整性valid=true/errors=[]。本次未發現本輪功能失敗，不需額外修補或重製APNG；保留先前已知其他範圍問題，不聲稱全專案零bug。
