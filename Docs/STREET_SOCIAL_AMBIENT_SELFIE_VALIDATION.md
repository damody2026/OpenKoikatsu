# 通用自主自拍驗證

原作 Shooting 的站立循環 f_tachi_00_07_00 與 WaitPoint type8 左手手機；共用曲線307587 bytes，未建立人物卡專用Cook資源。沿用共用 Ambient 擁有權、準備60秒上限、道具清理及前景優先；手機／飲料／自拍公平輪替。

Development 編譯30.55秒成功，47項既有Python測試通過。
Runtime-Runtime-StreetAmbientSelfieSmoke01：五女角 runtime APNG GPU 姿勢、左手手機、準備取消5次、對話中斷5次、自然8秒結束恢復行走及存檔不變皆通過；10271次道具更新。來源狀態／掛點／曲線SHA、五人GPU admission、五張1920×1080 PNG解碼獨立檢查通過。已查看角色1截圖，確認伸手自拍且手機跟手。

六名暖快取可見9.898秒，非冷啟動、百人載入或FPS成績。既有功能回歸及新版本發布進行中；完整人工驗收、原作完整自拍控制器／相簿介面與其他世界內容仍待完成。

同版 Runtime-StreetAmbientSelfiePhone01 及 Runtime-StreetAmbientSelfieDrink01 五人專項／獨立來源核對通過；三項專項執行檔SHA一致。Runtime-StreetAmbientSelfieInvitations01 20組接受／婉拒／取消及持久紀錄專項與獨立音檔／前後存檔核對通過。啟動selection仍為 npc_action_preload_count=0，共用曲線登錄23份、自拍僅1份。

截圖2及5亦已查看，後續人物因測試站位重疊而部分遮擋，不能以PNG成功解碼宣稱五人視覺完整驗收；當前五人功能依實際私有GPU動作、骨骼／手機transform、移動與清理斷言確認。

此次五人自拍私有資料實際 admission 準備時間1079.340～1814.834毫秒（既有暖快取環境）；保持準備上限60秒，不推論首次冷載入成績。發布完整回歸證據混用舊執行檔的負向測試已通過：拒絕且未寫receipt。現有公開版本ValidateOnly也通過，維持舊receipt相容。

## 發布與既有功能回歸

Runtime-StreetAmbientSelfieFull01 通過，errors=[]；15一般話題、5開場／5道別、表情轉換、收集話題／觸碰、名單／小地圖／導航／戀人呼叫／一對一、各共同活動與三種設施／取消恢復均通過。獨立848份來源語音SHA、1230對話＋270觸碰0列錯誤、96張1920×1080 PNG解碼通過。戀人實際走來320.116公分；六名暖快取可見8.805秒，不是百人或冷啟動成績。

公開 StreetSocial.cmd 的 receipt 已更新 StreetAmbientSelfie01.stage，執行檔SHA256：34a6167349b21b1695c2be9d67148fadd7778fb6138540c8818b5db24c3fe84b。保留舊receipt備份；runtime_npc_phone／runtime_npc_drink／runtime_npc_selfie／runtime_phone_use／runtime_invitation_ledger 均 true，full_acceptance=false。新版ValidateOnly通過；完整回歸混舊執行檔、自拍專項錯誤執行檔SHA的兩項負向測試皆被拒絕且不寫receipt。

本輪通用自主自拍功能實作成功。完整人工畫面／GUI／聽音訊／長時間驗收、所有其他原作活動與場所仍未完成；性能依使用者指示暫緩。
