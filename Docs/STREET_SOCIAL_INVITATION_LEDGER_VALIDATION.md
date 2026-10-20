# 邀請持久紀錄：功能驗證

## 當前實作
- 所有 runtime APNG 角色依穩定身分，使用同一套邀請規則。原始性格／關係成功率及語音不變。
- 吃飯接受與拒絕均記錄至下一遊戲日；結束交談與重新讀檔不清除。同行可再次提出，但必須剩餘兩次交談額度。
- 語音序列成功完成才原子保存一次扣額度；保存成功才開始活動。尚未完成回應時取消或替代對話、語音失敗及保存失敗不啟動活動；回應已保存後退出活動不退額度。
- 本場景依既有日期冷卻重置，原作依 NPC PopInit 配置初始化重置；不宣稱完整原作時間流程。

## 已通過
- Development 最終編譯成功；現用11份 StreetSocial 來源與隔離host一致。未整合的 OKStreetSocialPerformance.h 不在編譯依賴中，明確排除。
- 45 項既有 Python 測試通過。
- Runtime-StreetInvitationLedgerSmoke01：五種性格、六名 runtime APNG／GPU ready；20個接受／拒絕語音案例，10接受、10拒絕、20取消重試、20存檔規則檢查，errors=[]。
- 每個案例保存 invitation-ledger-NN.before.json／after.json。獨立腳本核對交談額度恰減1、唯一交易、吃飯until_day=day+1、同行無冷卻、好感／興奮度／關係不變、其他APNG資料不變；來源節點及實際音檔SHA全部通過。
- 原生場景另檢查讀檔、重開交談阻擋吃飯、隔日恢復、額度不足、身分隔離及重複交易不再扣額度。

## 驗證界線
五性格實際播放範圍是 stage0 的接受／拒絕，測試使用隔離的0／100成功率fixture並立即還原；不冒充三階段全部人工聽音完成。完整人工操作／世界內容驗收仍留最後；本輪不進行性能驗收。公開版本已在既有功能回歸及獨立核對通過後更新。

## 最終整合與發布
- 公開入口：StreetSocial.cmd → StreetInvitationLedger01.stage；保留 StreetQueueReuse01.patch，RuntimeV3 共用資料。發布器要求20組邀請、20次ledger檢查及獨立存檔／來源證明，不能沿用舊的邀請通過標記。
- Runtime-StreetInvitationLedgerFull01：passed=true、errors=[]；15個一般有聲話題、5開場／5道別、4收集話題交易、3觸碰、名單導航／五位戀人私有模式及電話實際走動320.28cm，合照、坐談、飲品、閱讀、同行、10組潑水、10組打西瓜與10次實際接觸、三個設施全部回歸通過。
- 獨立核對848來源音檔SHA、五位戀人stage2語音、1230對話／270觸碰來源列0錯誤；86張1920×1080 PNG完整解碼，實際檢視street.collected-topics.png。其餘完整逐畫面與聽音人工驗收仍待最後。
- 六人暖快取全部出現9.412秒；邀請專項13.852秒。這是六人功能測試載入資料，不是百人效能驗收。
- receipt原子發布及啟動器ValidateOnly通過；runtime_invitation_ledger=true、full_acceptance=false。
- 主專案Editor未停止、未Live Coding；正式使用者關係存檔未作fixture。所有新測試使用各自隔離的selftest存檔。

其餘未完成：原作旅遊／遊玩／體驗完整世界、其他場所／完整精油施術、住宿加成及完整原ADV等仍依既有世界計畫持續列為pending；本輪不宣稱已完成。
