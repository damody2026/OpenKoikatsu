# 告白結果對話與任何女角的一對一場景

日期：2026-10-08。候選版本：`StreetPrivateAny01.stage`。

## 行為

- 告白接受與婉拒都使用原作男女台詞、女角語音與表情；移除舊的 ADV 文字摘要。
- 好感 100 的必定接受規則保留；婉拒測試使用朋友階段好感 70 的原作機率，沒有讓 100 好感故意失敗。
- 從女角名單或目前交談對象可進入獨立的一對一場景，不受關係或距離限制。其他女角留在街區。
- 一對一使用當前關係階段的原作開場、話題與互動；進出不自動改變好感或關係。
- Esc、退出、取消或完成道別後返回街區，還原位置、AI、旋轉、鏡頭與游標並清理場景物件。
- 電話呼叫保留原本的戀人規則。

## 驗證

已通過 53 項 Python 測試。`Runtime-StreetPrivateAny01` 已完成五位女角的接受與婉拒、20 組原作問答；每種結果都檢查播放前確認、取消、完整播放、確認保存、重載與重複確認。獨立比對原作 ADV、原作拒絕列與來源語音順序。

一對一測試已完成五位女角 × 初識／朋友／戀人共 15 組，包含遠距離名單入口、來源開場、其他角色隔離與完整退出還原。`private.any.1.0.png` 人工確認初識女角獨立場景及退出入口；沒有其他女角出現在場景中。`Runtime-StreetPrivateAny01Full` 完整互動回歸已通過，`passed=true`、`errors=[]`、`private_any_verified=15`。獨立 contacts 驗證已通過 15 組當前關係階段的來源開場與語音 SHA-256；五位戀人的既有開場及電話到達／取消也通過。

四種自主活動各五名女角、20 組邀請、話題／觸碰、聯絡簿、手機與來源布局的獨立驗證全部通過。已發布 `Saved/StreetSocial/Build/receipt.json`，`StreetSocial.cmd` 使用 `StreetPrivateAny01.stage`；啟動前檢查已通過。

執行檔 SHA-256：`a2b079b5bbecdb626e2acd4bcdca5e71bc87f298d5246a609b0c5e207b3eeca1`。發布記錄 `runtime_private_any=true`、`private_any_verified=15`、`runtime_confession_outcomes=true`；保留前一份 receipt 備份。測試均使用隔離存檔。

請先退出舊版遊戲再從 `StreetSocial.cmd` 啟動，已開啟的舊程式不會自動替換。
