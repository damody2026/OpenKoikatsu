> 後續更新：通用吃飯邀請日期冷卻與一次額度提交已在 StreetInvitationLedger01 完成，詳見 STREET_SOCIAL_INVITATION_LEDGER_VALIDATION.md。以下為前一輪歷史驗證，原作全部時間／世界流程仍未宣稱完成。

# 活動邀請與共用來源列修正

2026-10-08。StreetInvitation04.stage／Runtime-StreetInvitationFull02 已發布至原 StreetSocial.cmd。receipt與launcher ValidateOnly通過，full_acceptance=false。

## 實作
- 共用五性格／三關係／五活動75組邀請資料、130句女角回應；原作日文語音＋中文字幕，檔案合計2,994,839 bytes（唯一音檔）。
- 吃飯、同行從中文男主邀請開始，依原作 stage0/1 favor 機率決定回應，stage2接受。無住宿加成；無額外好感獎勵。完整回應後才分派既有 runtime APNG 動作／同行。
- 取消、婉拒、缺資料、語音失敗不啟動活動。任何替代對話／觸碰撤銷尚未完成的邀請，防止替代語音完成時誤啟動活動。
- Info.LoadData 原解析器漏掉朋友 Event0..3 的四組選項，導致後段12列位移。新 RuntimeV3 對話與觸碰全部重建，舊 RuntimeV2 保留；1230對話＋270觸碰逐筆列位置核對0錯誤。
- 先前錯列造成的第二次臉頰缺音已隨正確來源資料消失；缺音恢復測試使用隔離 fault fixture，不再宣稱原作此處缺音。

## 已完成證據
- 45 Python 測試通過；Development最終編譯成功74.86秒，14份source-host一致。
- StreetInvitationSmoke02：真實六APNG／GPU ready、20組有聲邀請：10接受、10婉拒、20取消重試，存檔不變；40次來源表情套用、缺表情0。資料涵蓋全部三關係，但此測試實際播音覆蓋五性格stage0，接受／婉拒使用0/100機率fixture；正式來源機率不修改。
- 獨立比對20組節點、性格／用途／成功失敗角色及來源音檔SHA通過。
- 最終 StreetInvitationSmoke03 使用相同執行檔與 RuntimeV3 再次通過20組邀請及來源語音核對；暖快取六人全部顯示13.934秒。
- Runtime-StreetInvitationFull02 passed=true/errors=[]：五人名單、導航、電話實際步行320.59cm、五人戀人私有模式／開場、15話題、5開場／5道別、4收集話題交易、3觸碰、合照、坐談、飲品、閱讀道具、同行、10組潑水、10組打西瓜實際命中與三設施生命週期全部通過。暖快取六人全部顯示16.398秒，不代表冷載入或百人性能。
- 獨立核對848來源音檔hash、15有聲話題／13正好感提交、五位戀人stage2正確開場、1230對話／270觸碰0列錯誤。86張1920×1080 PNG皆可解碼，實際檢視contacts.private.1與facility.eat；其餘完整人工畫面／聽音仍留最後。
- 公開入口目前使用RuntimeV3/catalogue.json；原RuntimeV2資料保留，RuntimeV2/expressions.json是未受此列解析影響的共用表情庫。

## 界線
原作旅遊／遊玩／體驗區的邀請資料已匯出，對應完整世界活動未冒充完成。跨日talkCommand、住宿加成、完整原ADV、全部世界設施及完整人工操作／實際聽音驗收仍待後續；full_acceptance維持false。此輪不做性能優化或百人FPS驗收。
