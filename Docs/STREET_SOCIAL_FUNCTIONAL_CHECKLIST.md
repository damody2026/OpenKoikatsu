> 2026-10-08 最新發布：StreetAmbientGroom01.stage／Runtime-StreetAmbientGroomFull01。新增通用無道具單次生活活動及原作站立整理儀容，依runtime片段自然完成恢復行走；手機／飲料／自拍／整理儀容輪替。四項五人專項、20組邀請ledger及既有街區回歸通過；49Python、848音檔SHA、96PNG解碼及新版入口檢查通過。入口 StreetSocial.cmd。見 STREET_SOCIAL_AMBIENT_GROOM_VALIDATION.md；完整人工與其餘世界內容尚未完成。

> 2026-10-08 最新發布：StreetAmbientSelfie01.stage／Runtime-StreetAmbientSelfieFull01。新增自主自拍，共用生活活動公平輪替手機／飲料／自拍；任意APNG私有GPU姿勢runtime背景建立。三項五人專項、20組邀請ledger及全部既有功能回歸通過；47Python、848音檔SHA、96PNG解碼與新版入口檢查通過。入口 StreetSocial.cmd。見 STREET_SOCIAL_AMBIENT_SELFIE_VALIDATION.md；完整人工與其餘世界內容仍待完成。

> 2026-10-08 最新發布：StreetAmbientDrink01.stage。手機專用排程改為共用生活活動，新增自主喝飲料與本人右手道具；五人飲料、五人手機、全部既有功能回歸及同版20組邀請紀錄核對通過，47 Python／848語音SHA／96PNG解碼通過。入口 StreetSocial.cmd。見 STREET_SOCIAL_AMBIENT_DRINK_VALIDATION.md；最終人工驗收與其餘世界內容仍待完成。

> 2026-10-08 最新發布：StreetNpcPhone02.stage。新增 NPC 自主停留使用手機、本人左手道具、背景有界準備及前景中斷／恢復行走；五人專項、全部既有功能回歸、同版20組邀請ledger及獨立來源檢查通過。入口 StreetSocial.cmd。見 STREET_SOCIAL_NPC_PHONE_VALIDATION.md；完整世界內容及最終人工驗收仍待完成。

> 2026-10-08 最新發布：StreetPhoneUse01.stage／Runtime-StreetPhoneUseFull01。新增通用 APNG 站立手機雙人活動及本人左手手機道具，五女角專項／全部既有功能回歸／同版20組邀請ledger補驗及獨立來源核對通過。47 Python、96PNG解碼、848來源音檔SHA通過。入口仍 StreetSocial.cmd。見 STREET_SOCIAL_PHONE_ACTION_VALIDATION.md；完整人工及其餘世界內容尚未完成。

> 2026-10-08 最新發布：StreetInvitationLedger01.stage／Runtime-StreetInvitationLedgerFull01。通用邀請額度及跨交談／讀檔吃飯紀錄已完成；20組專項、20次ledger、全部既有功能回歸與獨立存檔／語音核對通過。公開入口仍 StreetSocial.cmd。見 STREET_SOCIAL_INVITATION_LEDGER_VALIDATION.md；完整人工驗收留最後。

> 2026-10-08 最新發布：StreetInvitation04.stage／Runtime-StreetInvitationFull02，公開入口仍 StreetSocial.cmd。吃飯／同行先播放來源邀請及接受／婉拒，完整接受後開始；修正漏算選項造成的後段12列位移，RuntimeV3共用1230對話＋270觸碰重新建立、0列錯誤。45 Python、最終20邀請專項、全部既有回歸及848來源語音核對通過。詳細範圍與剩餘見 STREET_SOCIAL_INVITATIONS_VALIDATION.md。完整人工／全世界內容驗收尚未完成。

# 第一版街區核心功能清單

2026-10-08，最新入口 StreetSocial.cmd → StreetFacilityLifecycle01.stage。主代理直接完成，性能優化依使用者指示暫緩。

| 核心功能 | 目前證據 |
| --- | --- |
| 獨立自製街區、一名可控制男角、五名 runtime APNG 女角 | 六人實際 Development 場景、runtime GPU／無私人 Cook 斷言通過 |
| 日常／戀愛／性愛話題、匹配語音、嘴型與來源表情 | 最新15話題逐筆身分／性格／語音序列比對、表情轉換通過 |
| 男女雙動作串流、開場／道別與可取消對話 | 正常5開場＋5道別、双串流與取消回歸通過 |
| 好感與關係、原子保存、確認唯一提交 | 真實話題正負效果、保存回讀、有聲預覽／取消／唯一確認實測通過 |
| 搭話對象提示、鏡頭優先／Tab、活動點停留與恢復 | native規則、實際scene candidate與production Tick活動占用／釋放通過 |
| 卡片身分重新關聯、交談對象失效與語音失敗恢復 | 身分重播／碰撞拒絕及active actor取消、有聲admission故障／有效重試通過 |

第一版前五項核心功能已完成。最新測試與發佈證據見 STREET_SOCIAL_TOPICS_TOUCH_VALIDATION.md；有聲確認的詳細證據見 STREET_SOCIAL_VOICED_PREVIEW_VALIDATION.md。舊期證據保留在各階段MD，不代表目前仍有其已修問題。

留待最後：正式視窗人工操作、畫面／聽音訊比對、完整內容覆蓋與長時間驗收。未聲稱完成的首版範圍外內容：原作完整 ADV 友誼／告白劇情、其他世界設施、性愛動作场景、完整眼頸 gaze IK；首版原 ADV 關係事件維持明確文字預覽及確認。有聲確認测试使用自己的匹配招呼語音fixture，不冒充原作友誼台詞。

百人房間 CMD 保留；六人街區的測試不代表百人驗收。

最新入口與證據、兩張實際畫面已再核對，未發現新核心功能缺漏；核對範圍及美術／人工驗收界線見 STREET_SOCIAL_CURRENT_FUNCTION_REVIEW.md。此核對沒有新增建置或遊戲測試。

已補 runtime 共用遠景地表與薄霧，最新Development截圖的黑色地平線已消失。功能回歸與實際影像證據見 STREET_SOCIAL_BACKDROP_VALIDATION.md；主專案執行中的Editor保持舊DLL，公開CMD使用新版獨立Development。

2026-10-08 StreetInput02：修正所有角色 local +Y 朝向，加入 Ctrl／中鍵切換鎖定滑鼠及 UI，新增男主場景台詞與女角具名字幕。Python26／隔離 native14／實際六 APNG 場景皆通過；完整桌面輸入人工驗收留最後。最新證據見 STREET_SOCIAL_INPUT_DIALOGUE_VALIDATION.md。

2026-10-08 新增可收集話題、女孩主動提問、持有話題回應、來源好感與興奮度／慾望、原子庫存保存。Python26/native15與真實場景4筆題目交易、原有15話題全部通過。最新證據見 STREET_SOCIAL_COLLECTIBLE_TOPICS_VALIDATION.md。

最終入口補到StreetTopics03：48個實際runtime拾取點、Runtime-StreetTopics04完整回歸及獨立語音／數值證據通過。
2026-10-08：話題循環與交談觸控已接至 StreetTouch08.stage，正式入口仍為 StreetSocial.cmd。Python26／native16／Development正式街區15正常有聲話題＋4話題交易＋3觸碰、取消與保存均通過，823來源音檔hash独立核對。觸碰可打斷語音，不誤結算舊話題，支援部位按鈕與私人GPU動畫骨骼點選。完整計畫及缺音限制見 STREET_SOCIAL_TOPICS_TOUCH_PLAN.md / STREET_SOCIAL_TOPICS_TOUCH_VALIDATION.md；最新入口以本筆為準，人工操作與聽音／內容全覆蓋驗收仍留最後。

## StreetTouch09 缺音恢復

原始聲音包缺少戀人第二次摸臉頰語音。通用 unavailable 資料保留＋會話次數前進已完成，第三次恢復原始完整反應，缺音／取消不提交。Python26/native16與 Runtime-StreetTouch05 全部通過，公開StreetSocial.cmd已更新；完整人工驗收仍待最後。詳見 STREET_SOCIAL_TOPICS_TOUCH_VALIDATION.md。

## StreetTouch10 輸入／字幕分流

UI面板內左鍵不再觸發背後女角的場景部位點選，缺音／拒絕提示獨立顯示，不覆蓋播放中男女字幕。Python26、native16、正式Runtime-StreetTouch06幾何回呼／播放中缺音／恢復／取消及原有全流程通過，公開CMD已更新。操作指南也已同步最新版輸入與話題流程。

## StreetTouch11 等待答案／交談操作

等待女生答案、無聲男主行及其他未完成交易會锁定語言切換，不覆蓋題目。取消／道別標籤與出口使用同一忙碌判斷，L／1～4／N只在前景有效。Python26/native16與Runtime-StreetTouch07正式等待／語言拒絕／取消不消耗／再次搭話及原有完整回歸通過，公開CMD已更新。人工輸入驗收仍保留至最後。

本輪新增：五位 APNG 普通雙人合照、性格動作分組、兩人背景 prewarm 同幀開始、五張 PNG、準備排隊取消/重試、拍照立即結束與雙方/相機/滑鼠恢復，Runtime-StreetPairedPhoto03 通過。其餘原作活動與完整人工驗收仍未完成。

2026-10-08 StreetSeated06：新增男女坐下聊天，共用來源椅坐曲線＋runtime APNG 私有姿勢；沿用中文話題、語音、嘴形/表情與原作效果，坐姿不被站立gesture覆蓋。五人語音fixture/取消恢復與完整功能回歸、獨立來源核對通過，詳見 STREET_SOCIAL_SHARED_ACTIVITIES_VALIDATION.md。未完成項目仍包含共餐/配對遊戲、原作完整邀請/height blend與最後人工驗收。

2026-10-08 StreetBook02：六人閱讀書本跟私有右手，切走路/其它動作收起；新增共同飲品（五位女性各自APNG私有GPU pose、手持瓶、語音及取消恢復）。35項Python、Development、Runtime-StreetBookFull01與獨立來源驗證通過，正式CMD receipt已更新。詳細界線見STREET_SOCIAL_SHARED_ACTIVITIES_VALIDATION.md；完整人工驗收仍留最後。

2026-10-08 已補雙向潑水：五女孩×男女兩方向共10案例、各自APNG runtime私有動作、兩個來源循環、水花、準備取消重試與活動恢復通過，完整回歸 Runtime-StreetSplashFull01。來源/界線/剩餘內容見 STREET_SOCIAL_SHARED_ACTIVITIES_VALIDATION.md。

小地圖已完成：左上角、五女角依實際好感著色、固定人物標記／姓名避讓引線、中文5字／英文10字與姓名開關；右側所有行動選單直排可捲動。Full05實景回歸與獨立語音驗證通過，詳見 STREET_SOCIAL_MINIMAP_VALIDATION.md。

2026-10-08 雙人用餐完成：StreetMeal03 / Runtime-StreetMealFull02，五女角 runtime APNG 私有姿勢、男女進食、自製桌椅/餐盤/食物、湯匙跟私有右手、語音與取消/存檔恢復通過；36 Python與所有既有功能回歸、獨立語音核對通過。原CMD receipt已更新，詳見 STREET_SOCIAL_SHARED_ACTIVITIES_VALIDATION.md。剩餘配對遊戲/完整世界及原作邀請/身高混合、最後人工驗收未聲稱完成。


2026-10-08 打西瓜：五女孩×男女揮棒十組實際接觸、共用runtime接觸校正／GPU快取、取消還原及完整既有功能回歸通過。80張截圖／823來源音檔雜湊／38 Python核對通過，入口已更新；完整人工驗收保留，詳見 STREET_SOCIAL_WATERMELON_VALIDATION.md。

2026-10-08 StreetContacts07 / Runtime-StreetContactsFull07 已發布：每人一列＋單一共用操作卡，五女角導航、戀人實際走來電話／取消／撤回，一對一runtime APNG／五人實際來源戀人語音／退出還原；39 Python、91PNG、完整既有回歸與獨立語音核對通過。詳細界線見 STREET_SOCIAL_CONTACTS_VALIDATION.md；電話狀態為文字，完整人工／全世界／原作邀請及height blend仍待完成。

2026-10-08 設施通用生命週期完成：背景動作準備好才進入；準備取消不移位、60秒準備／使用分離、取消／離開按鈕、鏡頭及滑鼠還原。40 Python、三設施專用實景與Runtime-StreetFacilityLifecycleFull01完整既有回歸／獨立來源核對通過，原CMD已更新；詳見STREET_SOCIAL_FACILITY_LIFECYCLE_VALIDATION.md。原作完整邀請、步行設施入口、全世界場所／購物及最後人工驗收仍待完成。

## 2026-10-08：移動與鏡頭修正完成

- [x] 既有 Cook 地圖 runtime 修正裝飾鋪面高度及碰撞；四邊雙向膠囊穿越與保留建築阻擋通過。
- [x] 五名女角最終提交方向統一限速 45 度/秒，包含活動轉向與取消恢復；動畫頻率不變。
- [x] 男主左右滑鼠係數加倍，上下維持原值。
- [x] 同一 Development 執行檔的原有互動、四種背景動作、20 組邀請與獨立來源驗證通過。
- [x] 原本街區 CMD 已切換 Movement03，啟動前檢查通過。
- [ ] 最後完整人工驗收（另行進行）。


## 2026-10-08：對話 UI、個性站姿與跑步完成

- [x] 一般交談時雙方無書本，女角依個性選站姿，語音後返回站姿。
- [x] 右側點「拋出話題」展開選項；中下方顯示對話；兩者使用半透明圓角面板。
- [x] 右下六項數值滑桿即時保存／雙向同步，無「除錯」文字或額外保存按鈕。
- [x] Shift 男主跑步動畫；女角巡路偶爾跑步，270 cm/s 低於男主420 cm/s。
- [x] 專項、移動、完整原生回歸、四種自主活動、邀請與53項Python測試通過；原CMD已切換StreetPresentation05。

詳見 STREET_SOCIAL_PRESENTATION_VALIDATION.md。
