# 通用單次生活動作與整理儀容驗證

## 本輪實作

原作 Appearance.Appearance 0 使用 f_tachi_00_14，m_Loop=false；來源WaitPoint存在無道具Appearance。新增433403bytes共用曲線，manifest與runtime selection皆保留 loop=false。未建立人物卡專用Cook資源，私有GPU姿勢仍依APNG runtime背景建立、啟動不預載。

共用Ambient描述支援零道具與OneShot：無道具時不載入網格、不生成物件，不索引Props[0]；開始後使用實際runtime片段長度，等待播放器自然完成才恢復行走，超時清理不算自然完成。循環活動維持原播放時間，前景互動可取消準備或播放。手機／飲料／自拍／整理儀容四種輪替。

## 當前證據

Development編譯20.83秒成功。修正發布檢查錯誤順序後，49項Python測試通過（含新增兩項不依賴Saved資料的發布執行檔證據負向測試）；1230對話＋270觸碰來源對應0錯誤。

Runtime-StreetAmbientGroomSmoke01 passed=true、errors=[]；五名女角準備取消5次、對話中斷5次、自然單次完成5次、恢復行走、存檔不變通過，16886次本人骨骼取樣。獨立來源非循環狀態／無道具／曲線SHA、五人實際私有GPU admission、五張1920×1080 PNG解碼通過。已查看角色1截圖；截圖於動作早期採樣，不能據此宣稱完整動作視覺驗收。

六名暖快取可見9.570秒，不是冷啟動、百人載入或FPS成績。三項既有有道具活動與街區回歸已通過，發布結果見下方。

完整人工畫面／GUI／聽音訊／長時間驗收留最後；完整原作儀容狀態機、坐姿、體型混合與其餘世界內容尚未完成。

四種活動使用同一實際執行檔：整理儀容／手機／飲料／自拍各五人專項及獨立來源／GPU admission核對皆通過，彙整於Runtime-StreetAmbientGroomSmoke01/activities.validation.json。完整回歸混用舊執行檔與缺少game.log兩項負向測試也通過，拒絕且不寫receipt。

Runtime-StreetAmbientGroomInvitations01 的20組實際邀請語音接受／婉拒／取消、20次持久紀錄與獨立來源音檔及前後存檔核對通過。

## 回歸與發布

Runtime-StreetAmbientGroomFull01 passed=true、errors=[]；15一般話題、5開場／5道別、表情轉換、收集話題／觸碰、名單／小地圖／導航／戀人呼叫／一對一、各共同活動與三種設施／取消恢復均通過。獨立848份來源語音SHA、1230對話＋270觸碰0列錯誤、96張1920×1080 PNG解碼通過。戀人實際走來320.015公分；六名暖快取可見10.743秒，非冷啟動或百人性能成績。

公開 StreetSocial.cmd 已更新到 StreetAmbientGroom01.stage，執行檔SHA256：e0084c251348e17111fc4ce0048dfb3cd8ac552cb7972a5637e523fc1f1926cd。保留舊receipt備份，runtime_npc_phone／runtime_npc_drink／runtime_npc_selfie／runtime_npc_groom／runtime_phone_use／runtime_invitation_ledger均true，full_acceptance=false。新版ValidateOnly通過。

四項發布負向核對通過：混用舊完整回歸執行檔、缺少實際日誌、整理儀容缺少自然完成、專項執行檔SHA不符。皆拒絕且不覆蓋既有receipt。新增兩項不依賴Saved資料的發布單元測試，全部49項通過。

本輪通用無道具單次活動功能實作成功；整理儀容私有GPU準備實測1539.548～2268.294毫秒（既有暖快取環境）。目前仍為場景輪替的活動選擇，原作完整個人生活日程／偏好、其他動作與場所，以及最終人工驗收尚未完成；性能依使用者指示暫緩。
