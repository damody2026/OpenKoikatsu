# C018 多材質停噴卡頓 E295

2026-10-05。正式 CPU CardReader；本輪修正共用油漆流程，不修改卡片、不減少人物面數或貼圖解析度。

## 根因與修正

之前 E294 的 C000/C001 局部固定點測試不足以覆蓋本例。使用者實際 CardReader 185919 日誌包含頭髮、臉及多個衣服部件接觸，停止後幀號前進變慢。C018 固定腰部、裸手臂未重現，橫向上半身掃射才重現油漆 actor 最大 350.24 ms。更高的頭髮/臉部掃射亦發現多材質塗層更新瓶頸。

- 殘留液體直接留在原格點，避免整數位置重新插值的浮點擴散。三角面內流動使用預算的重心座標平移；跨面仍使用原精確鄰接查詢。
- 姿勢、世界座標、步長、黏度或格點大小改變時使映射快取失效。每表面最多 65,536 個跨面路由；額滿仍做精確查詢。保留動畫後重力投影、非負厚度與 double 質量帳本。
- 重用厚度陣列，預算每面法線/面積，整數換算 padding；實際位置未改變時避免重做全表面 normals/BVH refit。
- 塗層逐幀檢查當前頂點，但只在姿勢、component transform 或拓樸改變時更新 procedural mesh。靜止不再每幀重送相同網格。
- 厚度實際變動、入射、滴落或清除才標記 dirty。兩張 ping-pong RT 各自累積待更新 tiles，包含移走油漆的清零資料；不會因只改一張貼圖而回復舊污漬。
- 每表面至多 16 個上傳矩形，保留 tile 原解析度，矩形內仍填入現在的濕區/乾區值。避免原本整列乾區複製，也避免大量 tiny RHI copy。未更新的厚度不再每幀計算平均或轉換 Float16。
- 即使厚度靜止，材質顏色仍可獨立更新。

## 實測

正式 CPU、1600×1000、單卡 Tpose。計時以每幀 actor 累計及 wall/frame 差為準，排除截圖 readback；數值屬本機測試，不宣稱任意動畫或硬體相同 FPS。

| 案例 | 停止期平均整幀 | 油漆 actor 平均 / 最大 |
|---|---:|---:|
| C018 上半身掃射，原版噴3/停5，HeadBaselineR1 | 70.60 ms | 65.61 / 350.24 ms |
| C018 上半身掃射，AcceptanceR1噴3/停5 | 6.77 ms | 3.28 / 33.61 ms |
| C018 頭髮/臉，第一階段修正 HairFinalR1噴3/停15 | 40.35 ms | 36.64 / 75.70 ms |
| C018 頭髮/臉，HairAcceptanceR1噴3/停15 | 6.53 ms | 3.62 / 43.70 ms |

上列最終量測為 BuildR10；兩項 valid=1、exit0、image_checks valid=true。質量守恆約零、重心下降、simulation<=64、impact<=16、jet預算512，真實半脫/全脫/穿回及另存離線回歸通過。頭髮/衣服流痕截圖已檢視。APNG不需重製。

Paint295R4 使用真正 RHI：ConservativeTransport、Film、MaskParity、Scene 四項 Success。包含快取/未快取厚度一致、變形後失效、有限非負、守恆、殘留不擴散且不要求貼圖上傳。失敗試驗與原因保存在 Docs/ERROR_LOG.md，不把 failed/NullRHI 或局部測試當全功能成功。

## 操作

關閉舊讀卡視窗後重新執行 `CardReaderDevelopment.cmd`，載入同一張 C018。A＋左鍵噴射，R 清除。原 APNG 與已準備的服裝狀態資源保留。

## 最後正式建置驗收

BuildR12成功；DLL SHA256 `C7016EF11775852554DDE7E1D550FAD9BB511097C7CFEA6DA28E93CBED5E6169`。Paint295R5 真正 RHI 四項 Success，新增靜止塗層不重送mesh、靜止時改色成功的實際Scene測試。RenderFaces按face ID排序，避免TSet迭代順序改變誤判拓樸。

Reader295FinalUpperR1：C018噴3/停5，平均整幀15.533918ms，paint平均6.071284/最大33.714600ms。Reader295FinalHairR1：噴3/停15，平均整幀16.055862ms，paint平均7.000142/最大27.733702ms。两項valid1/exit0/image_checks true；與之前R10不同時間的量測會受本機負載/幀率影響，最後正式版本以本段為準，不把先前6.53ms當最後版本保證。

兩項下流重心分別下降9.18/2.51cm，守恆約零、simulation<=64、impact<=16、jet預算512、真實穿脫/穿回/另存離線均通過；C018載入1.67秒。測試仍有個別油漆更新峰值約34ms，沒有把「持續数百ms負載解除」說成所有幀零卡頓。

本轮原卡回歸曾測C001載入10.285299秒，保留該實測；水槍修正沒有更動APNG載入解碼，不宣稱本輪重新達成全100卡十秒載入或全720outfit性能驗證。

Reader295FinalOriginalR1：C000/C001平均整幀2.797082/3.630545ms，load2.408374/6.585781秒；兩卡水柱命中、下流、守恆、穿脫/穿回、另存離線全部通過，valid1/exit0/image_checks true。最後三組驗收的DLL hash相同，結束後再次核對仍未變。截圖已檢視。

### E295 再次完整驗收（2026-10-05）
使用者要求繼續並最後驗收。正式DLL仍為C7016EF11775852554DDE7E1D550FAD9BB511097C7CFEA6DA28E93CBED5E6169；啟動器DryRun核對CPU/output/CrowdAPNG，先前同版C000/C001回歸與Paint295R5四項Success均已核對。本次未新增程式修改。
Reader295RepeatUpperR1 / RepeatHairR1 正式CPU、1600×1000、C018：噴3/停5及噴3/停15均valid1/exit0/image_checks true，實際截圖已檢视，命中/下流/守恆/穿脫/穿回/另存離線全通過。
停噴期平均整幀5.826514/5.097292ms，paint平均2.963988/2.946916ms、最大47.081400/36.365397ms；與前輪整幀15.53/16.06差異保留為同機負載/幀率變動，不承諾每幀零卡頓。未重現持續數百ms負載。simulation<=64、impact<=16、jet預算512；下流重心下降9.268317/2.214132cm，守恆約零。載入1.917107/1.946918秒。
第一個程序RHI初始化等待较久，後續正常進入場景，不計入APNG load_seconds或假稱噴射卡頓；保留啟動日誌。本輪功能測試沒有新的失敗，無需重製APNG或新增修補。驗收前後DLL相同，本輪完成。


E295 完成狀態核對（2026-10-05 19:49）：正式DLL hash仍與三组成功GUI驗收相同，相關水槍source與測試source均早於DLL建置；Engine/project BuildId相符。啟動器仍為CPU/output/CrowdAPNG。C018兩種掃射、C000/C001回歸均valid1/exit0/imagevalid，四項Paint automation均Success。沒有新程式變更、失敗或未完成項目，故本次核對既有實測證據，不重跑同版本相同測試。
