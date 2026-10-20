# StreetSocial runtime 表情平滑切換計畫
2026-10-07。接續已通過五人測試的 StreetFace03，不重作已完成部分。
1. 讀取共用 expressions.json 每個眉／眼／嘴 pattern 的 blend 旗標；缺省 true，錯誤型別拒絕。
2. KKS MorphAssist/TimeProgressCtrl 的預設為 0.15 秒線性 pattern 過渡；false 即時切換。每張 APNG 私人 morph 在 runtime 使用，無卡片特化 Cook。
3. 以有界 morph 權重表示目前狀態與目標，快速重定向從當下狀態開始；嘴型與眨眼保持更新，取消復原歷史路徑。
4. 同步主網格與 depth 的頂點；既有 60Hz 更新，過渡完成後不持續強制更新。觀測實際開始／中間／完成／取消計數。
5. Codex 獨立審查 Grok 差異，Development／Editor 編譯、純規則與 morph 測試、正式街區六張 APNG 功能流程；通過才更新公開 receipt。
不擴大到 blush／tears／highlight、頸IK、全部940列手勢及完整ADV移植。最後完整視窗／性能／視覺驗收仍獨立列出，不以功能測試取代。