# APNG 衣服資源識別與另存續修 E291（2026-10-05）

重新啟動 `CardReaderDevelopment.cmd` 使用新版 0998。100 張正式 APNG 卡仍預設開啟 `output/CrowdAPNG`。

## 修正

E290 以整張 PNG SHA256 作衣服狀態快取鍵。更換 APNG 預覽、名字或 Custom 身材值會改變檔案 SHA256，雖然內嵌模型完全相同，卻失去已準備的半脫資源。E291 改為已通過 CRC 的 `okPv` 內嵌資源 SHA256，快取位於 `.okstates/<內嵌資源 SHA256>/outfitN_<九個狀態>.okpreview`。目前卡片的 Custom 仍在載入後套用，修改身材不會讀回快照的舊身材。換模型資源則換鍵，服裝索引與全部九狀態仍分開。

舊整卡 SHA256 目錄保持可讀，載入時遷移至新目錄。只讀舊快取若無法遷移，仍可讀取舊狀態；不刪除舊檔。暫存檔採程序 ID＋遞增序號，避免同程序併發與殘留暫存碰撞，寫入／同步失敗不留下半成品，已存在相同內容可重用。

讀卡畫面的普通另存與四視圖 APNG 另存，會將已準備的狀態複製到輸出目錄。不依賴卡名猜對應；先確認兩張卡的內嵌資源相同，只複製合法服裝／九狀態檔名並驗證快照 CRC／解碼。若原卡有有效 verified receipt，保留原始來源 SHA256，記錄 derived_from_apng_sha256、resource_sha256，更新新卡的整檔 SHA256，讓新卡之後仍可從 verified 原始資源準備其它狀態。複製失敗明確顯示警告，已保存的卡片保留。

`OnClothesState` 亦遵守載入中的 busy gate；disabled 按鈕以外的程式 callback 不能改變待載入狀態。

## 原始資源限制

準備過的狀態可完全離線載入、換預覽及套用身材。尚未保存的原生半脫模型仍需原始資源首次生成；不能從不存在的幾何變出真正的原生半脫。這輪沒有把所有舊卡的所有衣物组合批量保存，也沒有以逐卡 offset 或假隱藏替代。

手動拷卡到其它機器仍需帶上 `.okstates` 與專案共用資源；介面另存會自動帶上已有的狀態。原始 100 PNG／來源卡未改動，只增加派生狀態快取及测试輸出。

## 證據

- `Saved/Tests/Reader291BuildR2.log`：KKUnityBundle 與 OpenKoikatsu 模組建置成功；正常 DLL 与測試 0998 SHA256 相同，依賴 hash 記錄 `ModuleSnapshots/0998/verification.json`。
- `Reader291CacheTestsR2.log`：兩項通過。預覽／Custom 改變保持鍵，模型改變失效、CRC 壞資料拒絕；成功與失敗發布均不留下暫存，重複內容可重用。
- `Reader291LibraryR2.log`／`Reader291Library.json`：C000、C001 全部 14 個穿著服裝可讀，private rig 重複零時間求值無累積且頂點有限。各 outfit0、outfit1 的半脫／全脫／混合，共 12 狀態，衣物和身體可見性、節點、索引、UV、body mask 与原生一致。換 PNG ancillary 預覽資料後，跨目錄複製快取／派生 receipt 檢查；刪除本測試產生的 receipt 後仍可離線載入，身材調整有效。
- `Saved/Tests/PaintSpray396/Reader291RegressionR1`：Camera.KksMouse、Paint.Film、Paint.MaskParity、Paint.Scene 四項 Success、exit0。
- `Saved/Tests/Reader291GuiR1`：真正 Gallery 两卡依序噴射／半脫／全脫／穿回；busy callback 拒絕，同卡重開不重新抽取。使用 CardWriter 與介面共用 copy helper 跨目錄另存，無 receipt／空 game root 下重新載入半脫，再穿回；兩卡身高 0.123、衣物實際三角形及水槍 Actor 可見性均核對成功。两張 Jet 截圖已檢視。
- 測試沒有用實體鍵鼠點擊四視圖另存流程；該保存 callback 已接入同一 helper，FKKAnimatedPng 保留私有資源 chunk 的既有邏輯未改。此輪不是 FPS 壓測，不宣稱所有卡／服裝狀態組合或封裝版本均已驗證。

Rust重跑：`cargo test --manifest-path Tools/ok-extract/Cargo.toml --release --lib clothing_cache_ -- --test-threads=1`，及 `clothing_states_survive_preview_edits_and_outfit_changes -- --ignored --test-threads=1 --nocapture`。

UE重跑：以新 Tag 先執行 `Tools/run_paint_performance_audit.ps1 -Suffix 0998 -Regression`，之後依序執行 `Tools/run_reader_regression_audit.ps1`；需要匹配的 audit host／Reader290Offline fixture。不要與其它 GPU audit 同時執行。
