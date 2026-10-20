# 好感度 100 告白與原作對話上下文

日期：2026-10-08。候選版本：`StreetDialogueContext05.stage`，資料：`RuntimeV4`。

## 行為

- 好感度達 100 時，初識與朋友都保證選擇告白接受分支。已是戀人時不重複扣次數或清除好感。
- 100 以下保留朋友門檻與原作機率；本次的滿值保證是街頭模式規則，沒有聲稱原作必定成功。
- 五個性格的告白從原作 Event 6 引言，接續原作成功 ADV 的全部男女台詞與日語語音；拒絕同樣使用原作分支。
- 播放完成後可確認保存；提前確認或中途取消不改變關係與剩餘交談次數。
- 一般話題使用實際抽中的原作分支引言，包含共用拒絕台詞所屬分支；generic 對話使用同容器中先前的男主角台詞。
- 連續女生台詞不沿用先前男主角問句；原作沒有男主角台詞的問候與道別不額外編造。
- 收集話題與邀請同樣使用原作男主角台詞。事件條件不足顯示中文原因，清除舊的問句。

## 資料與驗證

`build_street_social_context.py` 從本機原作 communication 表與 ADV 輸出五種性格、22 個原始語音，保存表格、劇情及語音 SHA-256。繁體字幕沿用現有原作翻譯，補足男主角缺譯與一筆接受台詞誤譯。

`verify_street_social_dialogue_context.py` 獨立比對實機選中的問句、女生回覆及所屬分支，檢查完整 ADV 台詞順序、原作檔案與語音雜湊，以及實際執行檔身分。

已通過：

- `Runtime-StreetDialogueContext05`：20 組來源問答、五名初識女角好感 100 接受、17 句完整接受語音、提前確認不提交、取消不改資料、保存重載與重複確認。
- `dialogue-context.independent.validation.json`：20 組實際問答與所屬分支、五段原作 ADV 順序與 22 個原始語音 SHA-256。
- 53 項 Python 測試、10 項原生 StreetSocial automation 測試。原生規則涵蓋初識與朋友在 100 好感時全部 100 種擲骰值都成功。
- 1230 列原作對話與 270 列觸碰資料布局沒有不一致。
- `Runtime-StreetDialogueContext05Presentation` 與 `Movement`：既有半透明 UI、即時滑桿、五名女角站姿、六人跑步與移動回歸。

候選執行檔 SHA-256：`7fa5c0032918b3270d06b7d36e7d87b6b9843e07c74ecd5ffc8dac190f5c9deb`。

`Runtime-StreetDialogueContext05Full` 完整回歸已通過：15 句一般話題、五名女角問候／道別、收集話題、觸碰、聯絡簿、設施、散步、拍照、坐談、閱讀、手機、飲料、潑水、用餐與十種打西瓜組合；無錯誤。

四種自主活動（手機、飲料、自拍、整理服裝）各五名女角與獨立來源檢查全部通過。20 組邀請接受／拒絕／取消及次數保存比對通過。聯絡簿、話題與觸碰、手機、資料布局及告白的獨立驗證全部通過。

已發布 `Saved/StreetSocial/Build/receipt.json`，啟動器 `StreetSocial.cmd` 使用 `StreetDialogueContext05.stage` 與 `RuntimeV4`。發布保留先前 receipt 備份；`run_street_social_development.ps1 -ValidateOnly` 已通過，包括告白資料與 22 個原作語音雜湊檢查。各 runtime 測試均確認未改動玩家的正式關係存檔。
