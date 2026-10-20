# 設施通用生命週期驗證

2026-10-08 StreetFacilityLifecycle01.stage / Runtime-StreetFacilityLifecycleFull01 已發布。原 StreetSocial.cmd 維持原路徑，receipt指向本版；exe SHA及launcher ValidateOnly通過。完整人工驗收留最後，full_acceptance=false。

## 實作

三種設施共用先prewarm／後進入流程。背景準備時人物留街區、房間道具隱藏；玩家可移動，準備完成時保存真正離開街區的位置／鏡頭／滑鼠，才切入自建房間與私有動作。準備取消釋放預約、丟棄延遲動作並清理道具；不搬動玩家。進入後新增右側離開按鈕，Esc仍可用。退出恢復雙方位置、角色AI／動作、鏡頭／滑鼠及清理道具。準備60秒與使用時間分離，移除固定20秒自動結束，循環動作持續至玩家退出或角色／動作失效。

## 驗證

- Development編譯成功48.18秒，40 Python通過，十二份source-host一致，相關修改diff --check通過。
- Runtime-StreetFacilityLifecycleSmoke01三設施專用實景 passed=true/errors=[]：3次準備取消不移位／重試，3次實際私有動畫與有限骨骼，3次將準備時鐘設為已過61秒但正在使用仍有效，退出人物／鏡頭／滑鼠／存檔／道具恢復通過。不是實際等待61秒的長時間測試。
- Runtime-StreetFacilityLifecycleFull01完整既有回歸 passed=true/errors=[]；設施以上斷言再次通過。五人導航／電話／一對一、15有聲話題、5開場／5道別、話題收集／回應、觸控、同行、合照、坐談、閱讀、飲品、潑水、用餐、十組實際命中打西瓜皆通過。
- 獨立核對823來源語音雜湊、15話題語音／13正好感提交、觸控序列[0,2,1]及五人戀人開場來源通過。
- 91張1920×1080 PNG均可解碼；實際檢查三設施與右側退出入口。六人 runtime APNG GPU就緒，沒有載入角色專屬Cook資產。暖快取全員顯示8.602秒，不是冷載入或百人性能驗收。

證據位於 Saved/StreetSocial/Runtime-StreetFacilityLifecycleFull01，主報告social.validation.json、三份independent/reward驗證及images.validation.json；source-host比對Saved/StreetSocial/facility.source-host.validation.json。

## 界線及剩餘

本輪沒有重製原作完整邀請／接受拒絕台詞、步行到實體設施入口、完整精油施術者、購物、全部世界場所或height blend。房間仍為本作自建、動作由玩家APNG runtime生成。上述未完成項目及完整人工操作／實際聽音訊驗收保留，不以本輪功能回歸當成全遊戲驗收。錯誤及處理記錄STREET_SOCIAL_ERRORS.md。
