# Stage381：完整 Gallery 非同步重開驗證與顯示更新合併

Stage379/380 的核心載入測試不包含全部 GUI 初始化成本。本輪以實際 game mode／Gallery widget／D3D12 執行 SelectIndex → OnLoad3D → background Session.Open → FinishLoad3D，保留正常角色、材質、相機、語音與控制項初始化；完成後等待 shader、至少四個 engine frames，並 flush render thread。這是準備檢查，並非 GPU 首次呈現時間；不含引擎啟動與初始卡片列表掃描，OS/DDC 與 native 資源快取沒有清除。

每次載入開始後，直接呼叫切換其他卡片／outfit 前後 callback，核對原 identity 受保護。完成核對 PreviewCardPath/Outfit、SelectedIndex、fresh actor、非空部件、native session 身形與所選卡片相同；第二輪使用實際 GUI 再次重開，不直接手動建構 actor 或假造完成回呼。

新增的 console/測試程式只在 WITH_DEV_AUTOMATION_TESTS 編譯。Tools/run_gallery_reload381.ps1 用 fresh Tag、獨立自有程序與 output，實際 exit0＋JSON valid＋兩輪成功才驗收。環境變數恢復；只修改該測試程序內 GameRoot，不 SaveConfig，不改原卡片。

GuiR1 第二輪 GUI 10.403秒，GuiR2 11.270秒；不能與核心測試5.577秒混用。額外段包含目錄／控制、語音、face/hand、animation catalog 等，並非 PNG cache 再失效。OKGUI_PROFILE 只在 OK_LOAD_PROFILE=1 印出段耗時。

共用修正：AKKPreviewActor 的 face/hand display batches 保持各 setter 的 native 狀態更新順序，僅合併最終顯示重建。嘴型／開合／眼／眉先更新既有 FaceTransitions/rate/manual flags，最外層 EndFaceDisplayUpdate 再執行原 ApplyMouth；支援 nested，平時各單次 setter 仍立即顯示。RefreshHandControls 先依序寫左右手既有 native override，再以原時間／delta0 求解及刷新一次，而不是各次 setter 刷新全身。沒有改 shader、mip、native FBS 演算法、手勢求解或加卡名特判。

正式 BuildR1/R2/R3/R4 均 actual0。GuiR3 臉部合併版本與 GuiR4 臉部＋手部版本使用主 DLL；23,039 個 morph/CPU 頂點/可見性值與立即更新完全相同（含nested），2,001個 body/follower骨骼相同（Equals tolerance0），動畫/face clocks不變。第一次完成後才執行獨立 parity oracle，oracle不列入載入耗時。

GuiR4 第二輪 GUI完成10.390秒、準備10.661秒，texture decode0，51部件。face_hand段約2.850秒；這輪資料不足以宣稱 batching 造成穩定或大幅速度提升。保留所有中間量測，不拿最小值作效能保證。此項完成的是完整GUI正確性與避免重複顯示工作的通用流程；March依然需要mesh/material及其餘控制初始化，沒有宣稱瞬間重開。

既有 CommonUI GameViewportClient routing 提示在baseline与final都有，保留記錄；本測試直接呼叫真實公開UI entry point，不宣稱已測硬體滑鼠或所有CommonUI input routing/GPU pixels/Shipping。原Stage380/R2封存維持不變。MaleGuiR1 actual0/valid1，23部件，23464個臉部值與2668骨骼姿態exact、clocks不變；第二輪GUI4.110秒／ready4.204秒，decode0。最終GuiR4與MaleGuiR1均使用BuildR4正式DLL。

保存Saved/Tests/GalleryReload381/R1.audit.json與snapshot，完整baseline／final／logs／receipts／來源及DLL。原380/R2共218項舊證據逐檔保留。通解僅改顯示更新時機，沒有宣稱全部卡／outfit／controller／點擊裝置無bug。
