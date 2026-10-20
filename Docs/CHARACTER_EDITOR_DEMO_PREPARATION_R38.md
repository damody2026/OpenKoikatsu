# R38：Demo 打水漂準備卡住

## 計畫

1. 使用正式共同動畫資源重現並分段量測。
2. 修正清單列舉解碼全包動畫曲線的共用路徑；保留 additive／overlay 正確性。
3. 準備期間允許改選，補上失敗路徑恢復操作與可定位階段的紀錄。
4. 以實際編輯器驗證打水漂、改選、再次選取及無效動作後恢復，記錄目前二進位驗收結果。

## 重現

`Saved/Tests/EditorDemoStuckNativeR38.log`：正式 `editor-demos.json` id=984。
原始 studio/anime/00.unity3d 約 810 MB；列舉 11 個 action_19 狀態需要 15501 ms，完整準備 17059 ms。
沒有證據顯示死鎖；長時間解碼且整個動作選單停用，讓介面呈現卡住。

## 修正與驗收

### 共用修正

- AnimationClip 清單以 TypeTree 名稱前綴列舉，避免解碼未選片段的所有曲線。
- additive controller 引用的片段仍檢查參考姿勢；runtime overlay 替換片段亦重新檢查，沒有取消此驗證。
- 已指定 controller owner 的動作只建立該 owner 的圖，保留來源、owner、覆寫、圖快取的獨立身分。
- 同次準備共用具檔名的不可變 SerializedFile 資料，精確片段依檔名及 path ID 解析；原有 1 GiB／4 個 payload 上限及離開作用域釋放規則保留。
- 已知精確動作鍵直接走背景 PrepareAnimationClip，讓來源解析與曲線解碼在同一準備作用域完成，不先用另一個 worker 重複列出清單。
- 準備途中選單維持可操作；多次選取依 generation 以最後選項為準，舊結果不覆蓋新選項。
- Studio 目錄或角色狀態不合法的提前返回，均恢復可操作狀態並顯示中文原因；失敗不清除目前播放。補上準備階段、耗時、鍵與結果日誌。

### 正式二進位驗收（2026-10-09）

`Saved/Tests/EditorDemoPreparationDirectBuildR38.log`：Succeeded，82.02 秒。

`Saved/Tests/EditorAcceptance/EditorDemoPreparationFinalGuiR38/`：使用實際 C019.png，mode=DemoPreparation，valid=true、5 cases、exit=0；launch.json 保存兩個 DLL 雜湊，腳本確認驗收前後未變動。

| 項目 | 實測 |
| --- | --- |
| 首次選取打水漂，準備後真正開始取樣播放 | 7687.88 ms；原始 duration=12 秒，非循環 |
| 準備途中改選，最後選項生效 | 45.09 ms |
| 無效片段失敗，保留目前播放且選單恢復 | 47.30 ms |
| 失敗後切換其他 Demo | 53.50 ms |
| 再切回打水漂，持續取樣 | 47.12 ms |

首次背景準備 7612.39 ms，套用角色只需 6.42 ms。**首次大型資源仍需等待約 7.7 秒**，不能宣稱首次即時完成或小於 300 ms；準備期間選單可繼續操作，暖載後切換已確認正常。
最初的 GUI 驗收亦通過 5 項，但當時同時編譯原生測試，首次 11639.36 ms；保留該紀錄，不當作最後版本的耗時。

`Saved/Tests/EditorDemoAnimationRegressionR38.log`：44 passed、0 failed、37 ignored。涵蓋原始／mod 控制器、索引取樣、additive 參考姿勢、資料重用及作用域釋放；未執行的 ignored 測試不計為通過。

`Saved/Tests/EditorDemoOwnerEquivalenceR38.log`：額外執行 selective_owner_graph_matches_full_catalogue，1 passed。所選 action_19 的 11 個原始狀態，與完整 4146 個狀態清單篩出結果逐欄 Debug 比對相同。

原生打水漂重現測試另外已成功；GUI 驗收還要求 selected key 一致、PoseMode=2、正 duration、至少 3 次新動畫取樣及有限角色變換，沒有只靠畫面訊息判定播放。

錯誤紀錄：`Docs/CHARACTER_EDITOR_ERRORS.md` E125、E126。
