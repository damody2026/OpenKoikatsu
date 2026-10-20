# 1268 天使服裝：保留場景指定的服裝

2026-10-21。實作 `d1996aa6e`。

`koikatu_cs0001268` 的兩個天使（來源 object 136、280）應穿白色浴巾，Unreal 卻顯示校服。此次修正轉卡流程，沒有替角色指定白色材質或換上另一個模型來對圖。

## 實際前後圖

上排修正前、下排修正後；每排左側是原作場景縮圖、右側是 Unreal 實際擷取。

![1268 服裝修正前後](images/OKStudio/Clothes1268_20261021/comparison.png)

[修正後原尺寸圖](images/OKStudio/Clothes1268_20261021/after.png)；[修正前圖](images/OKStudio/Clothes1268_20261021/before.png)；[PNG 尺寸與 SHA-256](images/OKStudio/Clothes1268_20261021/manifest.json)。公開圖片為擷取圖，完整解碼並檢查 IEND 後無資料，不含角色卡 payload。

## 根因與實作

- 原作場景兩個天使保存 `coordinateType=0`；該套 `ClothesTop=28`。原作 ChaList 的 105/28 是「バスタオル」，prefab 為 `p_o_top_bathtowel`，實際 renderer 為 `o_top_bathtowel_a`、`o_bot_bathtowel_a`。來源第 1 套 top=2，為校服。
- 原本 Studio 呼叫一般 `MakeOkCard`。它會經 `shown_outfit_first` 選展示服裝：第 0 套沒有外出鞋時，從其他套中選有外出鞋且部件最多的一套。此卡因此交換第 0、1 套，但場景仍按原本 coordinate=0 載入，結果穿上校服。
- 新增 `remake_ok_card_for_outfit`／`ok_okcard_make_outfit`／`FOkSession::MakeOkCardForOutfit`，由 Studio 傳入來源保存的 coordinate。轉換只去掉既有 OK 資源封包，不交換 Coordinate、Status、MaterialEditor／ABMX 等 plugin 的編號。一般單角色轉卡的展示規則仍使用原有 API。
- 明確選擇非第 0 套時，同時錄製該套的模型及 query；卡片 digest 繼續代表第 0 套，維持既有格式契約。錄製 card pack 期間略過最終 Session／baked 快取，確保暖載入也會收錄來源依賴；平常遊戲載入仍可使用快取。
- 無效服裝編號會在轉換／寫入輸出前拒絕。原卡不原地改寫；既有 bindings function table 大小不變，新增的是獨立 C ABI symbol。

## 驗收

- 最新 Editor／Game Development 建置成功。
- Rust 一般測試 418 PASS、466 ignored。具名來源測試另行執行，不能把 ignored 當作已驗證。
- `scene_1268_clothing_source`：兩個天使的來源卡相同；確認舊展示規則會選第 1 套；明確轉換第 0／1 套、重轉 OK 卡後所有來源資料塊逐 byte 相同；拒絕 -1／7；原卡不變；closed reopen 正確選到浴巾／校服，`game_reads=false`。先暖載入再錄製，模型清單仍非空。完整本機庫與獨立純追蹤資源庫各 PASS。
- 一般轉卡回歸：完整庫、純追蹤資源庫各執行該來源卡的 `kks-verify` 與 `okcard-check ... live`，均 MATCH；後者 recorded／closed／live digest 相同，`gaps=0`、`game_reads=0`。此項驗證一般展示轉卡仍維持原行為，Studio 保留服裝的路徑由上述具名測試及實際場景另驗。
- 已提交實作版本六項 Studio 自測全部 PASS（GameSelfTest、PatternTest、EyeSetTest、BloomTest、TransparencyTest、EditorSelfTest）；透明材質 48 組 GPU cases、101 張 PNG、13 張代表圖完整性／SHA-256 通過。
- 7 項 Automation Success：姿勢存讀、動畫接點、鏡片／MDK 材質與陰影。1268、6331 同程序重新匯入並出圖 `2 / 2`，真實畫面確認天使恢復白色浴巾，6331 手部／頭部姿勢維持正常。

最後建置日誌為 `1268-clothes-final-editor-build.log`、`1268-clothes-final-game-build.log`；Rust 為 `1268-clothes-final-rust-suite.log`；暖快取與乾淨庫來源 oracle 為 `1268-clothes-warm-oracle.log`、`1268-clothes-final-clean-oracle.log`。

本機證據位於 `Saved/OKStudio/1268-clothes-*.log`、`1268-clothes-oracle/`、`Verify-Clothes1268Accepted/`、`Verify-Clothes1268SelfTests/`、`Verify-Clothes1268Automation/`。本輪沒有修改共用模型或提交私有角色卡；純追蹤資源測試使用 `Saved/OKStudio/Clean1268/Content/IndependentResources/base_v1`。

## 使用與邊界

舊 `.okscene` 及其已轉出的角色卡已經交換過服裝資料，需重新匯入原作場景 PNG，才能取得正確來源服裝。新的卡片內容自然產生不同快取鍵，不要求手動刪除全域快取。

本次修正的是來源服裝選擇與轉換契約。畫面仍有光照、色調、表情影格等差異，沒有宣稱整張圖逐像素相同，也沒有宣稱任意缺失 zipmod 或所有未選取服裝的獨有資源已自動補齊。
