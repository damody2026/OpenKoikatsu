# 女角名單、導航、戀人電話與一對一驗證

2026-10-08 最終版 StreetContacts07.stage / Runtime-StreetContactsFull07 已發布。正式 StreetSocial.cmd 保持原路徑，receipt 指向本版；exe SHA 與 launcher ValidateOnly 通過。百人房間入口未修改。

## 新介面

每位女孩只有一列姓名／關係／即時距離，先選人再使用下方唯一一組導航／電話／一對一操作。選人不切換目前交談者。共同活動／設施及觸控預設收起，聊天選項優先；一對一隱藏共同活動，戀人隱藏友誼／告白升級入口。長字幕、提示與關係標題明確換行，實景確認無右側截斷。

## 實作驗證

- Development07編譯成功21.79秒；39項Python通過，十二份source／host檔案一致。本輪相關檔案diff --check通過；其它並行角色編輯文件的既有空白未修改。
- 完整正式場景回歸 passed=true，errors=[]。五女孩導航通路、不可達繞路檢查；名單五列／唯一三操作卡斷言通過。
- 電話呼叫實際行走320.209cm後近距離開始交談；非戀人拒絕、取消不瞬移、關係撤回後取消、重試及到達通過。
- 五女孩各自runtime APNG進入自建一對一空間；五筆實際戀人開場音訊完成，獨立核對來源stage2及性格／voice ID通過。退出恢復雙方位置、AI、鏡頭、滑鼠與存檔，清理六個共用道具。
- 既有15話題配音／5招呼／5道別、關係事件、取消、話題收集／回答、觸控、3設施、同行、5合照、5坐談、6閱讀、5飲料、10潑水、5用餐及10實際命中打西瓜完整回歸通過。
- 獨立823來源語音雜湊、15話題配音、13筆正好感提交、觸控重複序列[0,2,1]通過。91張1920×1080 PNG全部可解碼；實際檢查名單及私人聊天畫面。
- runtime證據確認六人GPU就緒、零GPU失敗，沒有載入角色專屬Cook資產；暖快取全員顯示10.667秒，不能當成冷載入或百人性能驗收。

## 修正過程與界線

01生命週期通過但未實播戀人開場，找到Introduction fallback id0與Speak never混淆，依精確來源metadata修正，普通Speak0仍禁止。03已通過新功能及獨立語音；03／04因使用者UI修訂停止自有測試。04實景發現收合符號缺字、戀人無效升級鈕及私人聊天閱讀待機，05修正。Full05舊西瓜probe誤把準備與播放一起算55秒，06限定準備階段；06實景又發現長提示截斷，07加明確換行後完整重跑通過。失敗／中止版本均未發布，詳細記錄STREET_SOCIAL_ERRORS.md。

電話目前文字顯示通話狀態，沒有冒用電話邀請語音；到達及私人聊天使用來源配音。一對一提供現有話題／語音／嘴型表情／觸控，不等於完整性愛動畫場景。測試使用獨立存檔及戀人fixture，沒有修改玩家的關係。人工操作與實際聽音訊及全遊戲驗收留最後，full_acceptance=false。原作完整邀請、height blend、購物與全部世界活動仍未全部完成。

證據：Saved/StreetSocial/Runtime-StreetContactsFull07/social.validation.json、contacts.independent.validation.json、collected-topics.independent.validation.json、voice.reward.validation.json、images.validation.json。


2026-10-08 更新：一對一已取消戀人與近距離限制；五位女角 × 三種關係共 15 組通過。電話仍要求戀人。最新證據見 `STREET_SOCIAL_PRIVATE_ANY_VALIDATION.md`。
