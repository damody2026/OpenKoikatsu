# 水槍活躍塗層更新：緩衝重用與區段容量（E289）

## 通用改動

每個表面保留頂點、法線、UV、索引、顏色、切線及scratch陣列容量，使用EAllowShrinking::No。姿勢頂點、法線、切線仍每幀重新計算；只有實際繪製face名單／順序或渲染容量改變時才重寫固定UV／索引／顏色。face ID列表必須逐項比較，不能只看數量。遮罩profile及頂點讀取清單每幀重建、完成後Reset，不能解參考上一輪map的指標。

每個表面也重用CoatingPose的矩陣、Morph、fallback容量。Prepare首先Reset有效內容及LOD／weights指標，Morph的count必須歸零後再SetNumZeroed，否則同容量時舊Morph會累加。正、負、零Morph權重回歸改為連續重用同一pose，仍符合引擎CPU蒙皮oracle。fallback及無效pose不能沿用上一幀有效內容。

最重要的活躍播放問題：舊路徑每次可見濕面數改变，呼叫CreateMeshSection，Unreal MarkRenderStateDirty導致代理／GPU緩衝重建。現在維持塗層face容量，超出時增加約25%空位（至少16面），足夠時UpdateMeshSection。unused三角形的三個點完全重合於實際塗層第一頂點、法線及切線也有效；不產生面積、不塗抹、也不把bounds延伸到遠離人物的位置。

這沒有改人物模型面數、油漆物理三角形、遮罩解析度、120Hz ballistics／20Hz表面時鐘或質量。代價是約25%的多餘塗層頂點及至少16面的小表面空位，仍需GPU頂點處理／上傳，不能宣稱零代價。整張surface清空時渲染容量歸零；緩衝peak容量隨surface的清除／失效釋放。

## 控制與對照

- ok.Paint.CoatingBufferReuse=0/1：每幀釋放重建／重用資料容量；預設1。MeasureBufferPair在相同凍結姿勢／Film交替8次測量，含UpdateMeshSection，兩邊都使用同一渲染容量，不是旧DLL的整體FPS對照。
- ok.Paint.CoatingTopologySlack=0/25：精確live vertex count／預設容量策略。性能工具新增-TopologySlack參數，實際cmd log驗證值生效。
- CoatingRebuilds／CoatingUpdates報告是暖機、噴射、停止期間的累積正常更新，報告先擷取，再做held diagnostics；不包含對照額外操作。
- 表面mask工作量估計濕面數×(圖塊texel數+32)，單一surface佔至少70%時保留逐面＋chart並行，均衡時批次。這是一般排程策略，不能據某個慢輪推斷原因。0991兩張的dominant_fraction=.262441／.163024，均選批次，沒有驗證它改善之前March的排程反例；反例仍保留。

## 編譯與回歸

Unreal 5.8.3，Buffer289ModuleBuildR7專案模組成功。Buffer289PoolRegressionR1的Camera.KksMouse、Paint.Film、Paint.MaskParity、Paint.Scene全Success。

Scene直接比對重建／重用的頂點、法線、UV、切線、顏色及索引；包含pose更動、Morph／非等比縮放、GPU遮罩讀回／清除、拓撲替換與多人物。新增兩三角形測試：先塗第一面，再塗第二面，渲染頂點數與區段重建次數保持不變，第二面UV確實更新、所有空位零面積、質量守恆。原30/60/120Hz與世界投影oracle仍通過。

本輪R1/R2曾在另一段Crowd診斷引用FSkinWeightDataVertexBuffer::IsWeightDataValid產生LNK2019／LNK1120。檢查目前引擎實作發現該方法未跨DLL匯出；改使用既有公開inline GetWeightData()!=nullptr，僅實際可讀CPU資料才掃權重，其餘走原Unavailable分支。沒有更改渲染權重、打開CPU retention或覆寫引擎。R3及後續建置成功；保留失敗log。

## 0992實際卡片：精確大小／容量策略

同DLL、匹配依賴、同card／outfit／walk／噴射設定；各暖機2秒、噴射8秒、停止8秒，D3D12。KKS March 48surface／86,992面；離線APNG KK_346095 46surface／72,601面，空遊戲根目錄。六次均valid1／exit0，真正動畫第0幀後持續變形66.07–68.88cm，質量誤差約1e-12，held遮罩零差、sweep零不一致、held live質量零差。

| 測試 | slack | 噴射整幀平均ms | 停止整幀平均ms | 停止Actor CPU平均ms | 區段重建／更新 |
| --- | ---: | ---: | ---: | ---: | ---: |
| ExactNativeR1 | 0 | 54.102 | 79.528 | 37.635 | 1071／2456 |
| PoolNativeR1 | 25 | 53.219 | 62.713 | 27.548 | 92／3493 |
| PoolNativeR2 | 25 | 42.209 | 51.309 | 22.166 | 98／4991 |
| ExactNativeR2 | 0 | 48.374 | 60.411 | 30.876 | 1114／3903 |
| ExactPortableR1 | 0 | 34.325 | 34.231 | 17.433 | 940／6580 |
| PoolPortableR1 | 25 | 23.264 | 26.690 | 12.741 | 78／10314 |

原始tag均加Buffer289及Animated，完整log/report保存Saved/Tests/PaintSpray396。Native第二對反向執行（先pool後exact），避免只比較一個順序。重建次數約减少91%；兩輪Native及APNG停止整幀均較低，但不同輪FPS／濕面分布仍波動，不能宣稱固定倍率或全場景60FPS。Native最新pool仍51.309ms，較慢pool輪62.713ms也保留。

0992相同凍結資料buffer重建／重用：NativePoolR1 5.370／4.794ms；NativePoolR2 3.454／3.130ms；APNGPoolR1 2.204／1.847ms。這是局部CPU對照；不能把凍結UpdateMeshSection速度當成活躍paint會重建代理時的性能。

## 交付及限制

0992 normal DLL及匹配依賴已核對，來源及全報告保存ModuleSnapshots/0992/verification.json。CardReaderDevelopment.cmd重啟使用。0991／0990／0989／0988中間報告與較慢值保留，不混入最佳FPS。沒有關閉另一專案的Editor。畫面解析度、物理頻率、人物密度及UV解析度不變。

DLL SHA256: 9ec81d442c1cf18b86cbbd3b00d856b864587c971dd3bd016da68448af6fb379
