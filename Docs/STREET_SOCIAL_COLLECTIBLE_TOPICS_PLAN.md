# 街區話題收集與女方提問計畫

2026-10-08，主代理直接完成，不使用子代理。

## 原作規則依據
- SaveData/Player.cs TopicState：庫存上限99，使用減一。
- Manager/GameAssist.cs GetTopic：性格對應話題群，避開最近5題。
- TalkScene.cs CommandFunc(3/4)、CommandWait：女方先播 introduction，男方選庫存；完全相同→success、同Category→normal、其它／敷衍→failure。common/rare表選值按原碼，包括 rare 同類使用 rare[2]（保留原行為，不擅自修正）。
- 男方主動使用話題：TopicTalkPoint 稀有度 favor；Category4轉入lewdness，非情色轉入favor；interest.answer匹配額外 TopicInterest 加成。原作情色主動話題限制非處女；APNG Parameter 並沒有 heroine.isVirgin（它屬於原作遊戲存檔），不能從外觀／卡名猜測。自製街區依使用者需求允許情色話題聊天，這項是街區規則差異，未冒稱保留原作存檔門檻。原作 desire4/5 各+10另行記錄，不將 desire 誤當 lewdness。
- 所有文字／語音從共用 TopicListen、TopicTalkCommon／Rare、TopicReply 與 Topic 表匯出，不依特定卡做資源。

## 實作
1. 匯出共用 topics.json，含數值、性格群、完整依序話題對話（男主文字、女角真實語音及來源表情）；缺檔整段拒絕，不亂配語音。
2. 原子存檔擴充男主話題庫存、每天拾取紀錄、女角最近5次提問題目與兩種興奮／慾望。舊存檔缺欄位以空庫存載入；不丟舊好感與卡片身分。
3. 自製街區放置可互動話題點，靠近 F 拾取，次日更新；物件只用共用形狀／材質。UI顯示名稱、類別、稀有度、庫存，供男方使用。
4. 對話新增「請她選話題」，播放她的引言後列可回應庫存與敷衍。播放結束才原子扣庫存及加／減原作數值。禁止連按重複提交，失敗／取消不消耗。
5. UI顯示來源男主文字、女角當前逐段字幕及興奮度；runtime APNG興趣／處女資訊決定加成及情色資格。
6. 原生數值／保存回歸與實際Development多步語音驗證通過後更新同一公開CMD。完整人工驗收留最後，不做性能優化。

街區拾取點配置／每天刷新是自製場景規則；不冒稱完整原作地圖掉落與祈禱系統。學習點(point)暫記錄但不接尚未存在的男主技能系統。
