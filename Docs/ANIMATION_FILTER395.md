# Stage395 搜尋選單操作與首次查詢

延續 Stage394 的共用 UOKMotionComboBox 搜尋，不改寫卡片、動作 row、完整原始片段鍵值或播放邏輯。

## 發現與修正

- 搜尋框原來只綁文字變更，Enter 不提交。現在搜尋框 Enter 選第一個符合結果；上／下鍵把焦點交給結果清單，清單 Enter 提交高亮結果，Escape 關閉。關閉時對仍在自製 popup 的 Slate user 恢復焦點到下拉控制項，沿用原 ComboBox 的焦點恢復方式。
- 原 SSearchBox 預設延遲 0.25 秒送文字變更，快速打字後有短暫舊結果。關閉延遲，Enter 也以提交文字重新驗證。提交前同時確認項目仍在 FilteredOptions 與完整 Options，拒絕被篩掉或來源已替換的 pointer；選單停用時不提交。
- 原清單把 selection-change 當提交，會混入鍵盤選取及 pruning；改由實際 OnMouseButtonClick 與 Enter 明確提交。導航只移動高亮。已高亮的項目再次點擊也能正確關閉選單。
- 名稱轉譯原本每個字都掃全部583條短語。新增首字索引，候選仍保留原 longest-first 順序，原文／中文及動畫鍵值皆不變。當前選項的譯文快取保留到下次開啟，只移除已不存在的選項，不必反覆冷建。

## 驗證

- BuildR1 actual0/main0420/plugins0420；SearchR1 搜尋與轉譯兩項 Success。補焦點恢復後 fresh BuildR2 actual0/main0421/plugins0421。
- 最終 SearchR2 actual0，OpenKoikatsu.Motion.SearchFilter 和 TranslationIndex 皆 Success。
- 測試使用真實 Slate SearchBox.SetText、ListView 的 Home／Enter 與 mouse-click callback，另驗證搜尋框提交、上下移動、Escape、停用、被篩掉的舊高亮拒絕、重開快取／來源替換、完整索引及同名原始鍵。
- 8751筆合成目錄最終首次轉譯搜尋9.528ms、快取搜尋2.638ms，兩者門檻150ms；R1分別11.347ms／2.695ms，原數據保留。沒有把快取測量當首次成本。
- 全部583條短語的單獨、來源前綴／未知Unicode後綴、相鄰重疊組合，共1749案例，與舊全掃演算法一致，另驗空字串與ASCII來源。
- AutoR1 actual0／validtrue／10cases：自動同步來源、同名 raw／目錄身份、跨來源 raw、重播、手動來源、取代與取消回歸通過；有效 commit 13.625–125.095ms，H94.346ms，所有 request／有效 commit 都在原150ms門檻內。H背景準備41.314秒期間782frames／698舊動畫 samples。這輪改善搜尋與操作；首次控制器背景解析仍需等待。
- 封存前共享manifest被其它建置改為main／plugins0412，第一次一致性檢查拒絕封存。保留其manifest、不停止其它commandlet；改以原SearchR2／AutoR1的InternalLoadLibrary記錄證明實際載入0421，另fresh SearchR3在現在的0412版本驗兩項Success／actual0，cold9.949ms／cached3.643ms、1749oracle仍通過。0412只補驗搜尋，沒有冒稱AutoR1也使用0412。

Saved/Tests/AnimationFilter395 保留這輪證據；Stage394 舊 audit 和其30項 snapshot 不覆寫。測試是 Slate callback／自動回歸，不是實體鼠標驗證或全部動畫逐一播放保證。
