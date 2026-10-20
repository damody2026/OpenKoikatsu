# 水槍三角白斑、瞄準與停噴性能修正 E294

2026-10-05；正式 CPU 讀卡模式。

## 使用
重啟 CardReaderDevelopment.cmd 使用新DLL。A＋左鍵噴射，R清除。油漆設定面板可捲動；水柱不透明度保留0–1。

預設上限：水柱512粒；飛濺16個系統（每個至多12粒，滴落效果為1粒）；碰撞及附著液滴合計64；地面污漬128。每個上限可在面板調整。Niagara噴射兩個emitter的總發射率按最長3秒生命及3.1秒安全餘量限制，降低水柱預算會重置Jet，不保留舊預算粒子。飛濺生成至少間隔.05秒，超出effects上限不建立；地面污漬淘汰最舊項，不移除人物coating。

## 共用修正
- 原流動把chart形狀與三角面質量分開更新，按面重新縮放並為空白面補Brush，導致三角足跡。正式runtime改用Transport：在變形後表面格點搬運實際量，跨幾何鄰接面，用三角格點權重保守分配；不建立triangle-sized fallback。浮點厚度僅做double帳本量化殘差修正；沒有放寬守恆門檻。
- coating使用共享頂點法線及一致偏移，避免相鄰面按不同face normal偏移形成裂縫。沒有增加人物面數，沒有隱藏原角色網格。
- 取消每幀VisualRandom噴射方向。預設散射由2度降為.15度（仍可調）。Runtime及audit共用解析低彈道AimDirection，彌補normalize速度造成的重力瞄準偏差；不可達目標不宣稱能命中。
- 只在已有油漆的稀疏格點及鄰接面搬量；限量生成飛濺/液滴/地面元件，避免停噴後不斷累積。

## 最終證據
- Reader294BuildR7成功；正式uproject/normal DLL。
- PaintTransport294R2四項Paint automation Success：ConservativeTransport、Film、MaskParity、Scene；PaintAim294R1驗證精確窄目標彈道、跨chart保守量、非負/有限厚度及不補整面空白足跡。
- Reader294FinalArmR1及Reader294FinalFlowR1均valid=1/exit0/image_checks valid=true。CPU、3840×2160、噴3秒後停5秒；裸手臂近拍已核對，衣服流痕连续，未見原鋸齒三角白斑。穿脫/穿回、另存離線、busy/reuse回歸通過。
- 停噴後每幀累計的油漆actor平均/最大：裸手臂0.100157/2.888501ms，衣服0.142517/3.867902ms。同期wall/frame平均整幀3.078688/3.225058ms。這是本機CPU單卡Tpose讀卡場景，排除4K截圖readback，不宣稱所有動畫/硬體相同FPS。
- 峰值碰撞/附著液滴54/64、7/64，飛濺16/16；水柱上限512。質量守恆誤差約0；衣服5秒流痕重心下落14.949729cm。
- 先前R2/R3 timer抽樣released mean並非完整每幀平均，性能結論以FinalArm/FinalFlow累計帳本為準。PerformanceR1是舊GPU-only工具，CPU body顯示為Procedural導致No displayed body，不能當有效性能數據。

前次100卡8.425秒載入結果屬E293版本；本輪沒有重跑100張，不把舊hash當新版本性能證據。本輪C000/C001首次load_seconds為2.074671/5.923513，改動沒有進入APNG貼圖解碼及匯入流程；不需要重製APNG。
### E294 使用者要求最後完整驗收（2026-10-05）
正式CPU DLL SHA256 BBB22E4CD77C5CBC30F276B89A41FCB32044865BF2EC1DA5DB6B18415BDA3CEC，驗收前後與先前FinalFlow相同。啟動器DryRun為CPU/output/CrowdAPNG。
Reader294AcceptanceArmR1 / Reader294AcceptanceClothesR1：正式uproject、4K、噴3秒/停5秒均valid1/exit0/image_checks true；截圖已檢視，未見原三角鋸齒白斑。水柱1/.35、命中、下流、質量守恆、穿脫/穿回及另存離線均通過。停噴後油漆actor平均0.323913/0.245138ms、最大5.105603/4.708901ms；同期wall/frame平均整幀7.495937/5.015856ms，較上一輪受同機負載影響而變動，但未重現196ms停噴卡住。只能描述本機單卡Tpose測試，不能承諾任何環境的FPS。
峰值simulation54/7（上限64）、impact16（上限16），jet預算512。此輪未出現新的功能測試失敗，不需要額外修改實作；原資源保留。驗收範圍維持E294水槍共用修正，未重跑全部720 outfit或E293 100卡載入計時。
