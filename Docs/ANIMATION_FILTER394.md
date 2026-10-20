# Stage394 動作下拉選單搜尋

打開動畫來源、遊戲動作、Studio 分類／動作、配對姿勢／階段等共用 UOKMotionComboBox，下拉清單頂部會有搜尋框。輸入名稱、分類或來源路徑；同時比對原文與繁中顯示文字，英文字母不區分大小寫。空白分隔的多個詞必須全部符合。右下方顯示符合數／完整數，清空輸入恢復全部選项，每次重開清單重設搜尋。

搜尋只建立保留原始 shared pointer 的 FilteredOptions，UMG Options 與 FindOptionIndex 不變；不使用 ClearOptions 來篩選，不把翻譯名稱用作動作鍵值。點擊結果或在清單內按 Enter 才走原 SetSelectedOption，因此沿用 Stage393 的目錄 row／完整 raw source key、自動來源選取與非同步準備。輸入、導航、無結果或清单 pruning 不提交動畫，也不清除當前選取。關閉搜尋選單後原動畫繼續播放。

原文＋譯文的查詢字串在當次開啟期間快取，不解析 Unity 控制器。清單以 SListView 虛擬化顯示，不為全部 8751 項建立 Slate row。每次開啟讀取最新 Options 並清理文字快取，避免非同步更換來源後保留舊結果。既有 UComboBoxString 保持負責完整選取資料與 callback，沒有改寫播放或 KKS 的動畫設定邏輯。

## 驗證

- BuildR1 actual0 / Result: Succeeded；main0404、KKUnityBundle0014、KKCardImporterEditor0014。
- OpenKoikatsu.Motion.SearchFilter：SearchR1 actual0 / Success。驗證原文／譯文、來源、多詞、不分大小寫、無結果、清空、同名原始鍵、完整索引、導航不提交、點選精確提交、來源替換與釋放 Slate。
- 8751 筆合成目錄的快取查詢 2.638 ms（門檻150 ms）。這不是首次翻譯建快取的耗時，也不是所有動作播放性能保證。
- AutoR1 actual0 / validtrue / 10cases：完整目錄與 raw 同名身份、跨來源 raw、自動同步來源、重播、手動來源、最新請求與取消回歸通過；有效 commit 保持 <150 ms，H commit 77.874 ms，背景準備期間 666 frames／563 舊動畫 samples。首次冷載入控制器仍需背景等待。
- 本輪沒有實體鼠標或全8751動作逐一播放驗證。GUI log仍有既有 CommonUI viewport 配置訊息與 recursive FlushRenderingCommands 警告；沒有將它們說成搜尋錯誤或宣稱全專案無警告。Build 的既有 VerifyAnimationCatalogNow Value C4701 也保留在原log。

證據放在 Saved/Tests/AnimationFilter394，未更動舊 Stage393 audit 或其它人的 commandlet。
