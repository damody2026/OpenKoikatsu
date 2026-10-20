# 水槍動畫性能續修：共用 chart 與覆蓋並行（E286）

## 改動

表面遮罩的 barycentric 圖塊座標與 inside 標記由解析度決定，每次更新只建立一份供所有三角形共用。內部 texel 的原始值直接讀原圖塊的對應 grid；保留 padding 插值與退化三角形的世界投影 fallback。先檢查殘留門檻，再計算重力；只在跨接縫時組合世界位置。每面共用面積與法線，移除重複清理迴圈。

覆蓋分成三個階段：GameThread 擷取骨架／Morph、準備圖塊與唯一頂點列表；工作執行緒對不重複頂點蒙皮，再按三角形寫入固定輸出範圍；GameThread 更新材質、PMC 及附著液滴。各三角形 UV tile 不重疊，不需要在 worker 上加鎖或改動 map。附著液滴的三角形即使已不在 Film.Active 也納入頂點列表。小工作量保持串行，避免大量小任务。

沒有更改模型三角形、UV 解析度、噴射／表面時鐘、黏度、接縫選取、保留厚度或質量求解。

## 驗證

`ok.Paint.CoatingParallel`：0 為串行對照，1 為預設自動分塊，2 為回歸強制並行。Scene 測試在同一姿勢／同一油漆資料以 Tick(0) 比較實際覆蓋頂點、法線、UV、切線、顏色與索引；逐幀附著、Morph、非等比縮放、GPU RT 讀回與清除仍測試。

Performance audit 在八秒停止測量結束、整幀報告已取樣之後，暖機並交替串行／並行覆蓋各八次，報告 held_coating_*。不推進物理、不上傳遮罩、不修改動畫或形體；檢查 held_coating_mass_delta。這是同一份資料的覆蓋 CPU 對照，不能當成完整遊戲 FPS 倍率。

Chart286BuildR1 完整 target 成功。後續 Coating286BuildR1 的 Engine link 因另一專案持有 Engine DLL 而 LNK1104；本輪交付用現有 Unreal 5.8.3 引擎二進位檔，`Pair286ModuleBuildR1.log` 的 OpenKoikatsu 模組建置成功。未宣稱完整引擎重建成功。0983 與匹配插件依賴保存於 `Saved/Tests/PaintSpray396/ModuleSnapshots/0983`。

最終四項回歸全部 Success：Camera.KksMouse、Paint.Film、Paint.MaskParity、Paint.Scene。包含新增的強制並行／串行實際覆蓋資料比對；遮罩舊世界投影 oracle 最大厚度誤差 2.38418579102e-7。兩張實際卡片的動畫第0幀後仍持續變形、停止8秒質量守恆與同姿勢對照皆 valid1／exit0，質量誤差約1e-12，同姿勢對照零質量變化。

March 原 KKS（48 表面／86,992 面）、KK_346095 離線 APNG（46 表面／72,601 面，空遊戲路徑）；不同人物不能作格式倍率比較。

| 測试 | 噴射平均整幀 ms | 停止平均整幀 ms | 停止水槍 CPU ms | 同姿勢覆蓋 CPU 串行／並行 ms |
| --- | ---: | ---: | ---: | ---: |
| Pair286NativeAnimatedR1 | 48.197 | 92.342 | 50.174 | 7.116／6.110 |
| Pair286NativeAnimatedR2 | 56.272 | 53.872 | 23.525 | 2.697／2.429 |
| Pair286PortableAnimatedR1 | 51.533 | 51.797 | 31.088 | 4.116／4.141 |
| Pair286NativeStaticR1 | 7.533 | 10.597 | 5.800 | 1.875／1.920 |
| Pair286PortableStaticR1 | 9.184 | 11.306 | 7.111 | 2.308／2.116 |

March動畫 R1 的同資料覆蓋約 7.116→6.110ms；APNG動畫 R1 約4.116／4.141ms，沒有明顯差異，不能宣稱每張卡覆蓋都加速。靜止場景本輪約11ms整幀。跨輪動畫波動仍大，最慢輪92.342ms；表面傳輸、滴落、角色動畫及其它整幀工作仍有成本。此次確認分塊覆蓋的正确性與部分場景CPU改善，**沒有證明整體FPS固定改善或全場景60FPS**。

運行0981的中間重測結果也保留（Coating286NativeAnimatedR1/R2、Coating286PortableAnimatedR1/R2），不拼接最佳輪FPS。最終測試使用同一0983及依賴快照；normal DLL SHA256 `4F4B7A020D3AF7A0CD697445FDE30465ED726BAC0A12A1F626CBC56E03C90316`。重啟 CardReaderDevelopment.cmd 使用。
