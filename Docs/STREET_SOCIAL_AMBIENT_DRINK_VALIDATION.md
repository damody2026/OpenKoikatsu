# 通用自主生活活動與飲料驗證

## 實作範圍

手機專用擁有權／背景準備／中斷機制改為共用 Ambient 活動；描述包含曲線、左右手掛點、共用網格、部件尺寸／材質／偏移、準備／播放／冷卻時間。手機與飲料輪替；單一自主擁有者、每人冷卻，玩家前景互動優先。

原作飲料來源 f_tachi_00_10、Drink WaitPoint type9 右手、p_o_koi_ei_cup00 已核對；畫面飲料杯為本專案共用自製幾何，不宣稱完整重製原道具。私人骨架姿勢依 APNG runtime 背景建立，最多60秒；未增加角色 Cook 資產、曲線或私人啟動預載。

## 當前證據

Development 編譯成功（63.60秒，含等待／規劃階段；執行器22.44秒），47 Python測試通過。Runtime-StreetAmbientDrinkSmoke01：五人實際飲料動作、右手道具位移、準備取消、對話中斷、自然8秒結束後恢復行走、存檔不變全部通過；獨立GPU私人動作日誌、曲線SHA、原作掛點、五張1920×1080 PNG解碼通過。角色1實際截圖已確認右手拿著飲料。

手機重驗及既有功能回歸進行中，完成後發布。最終畫面／GUI／聽音訊／全世界內容人工驗收仍留最後。

飲料專項：13753 次道具更新，六人暖快取可見 9.794 秒（不是冷載入或百人效能驗收）。Runtime-StreetAmbientPhoneSmoke01 五人手機重驗及獨立核對亦通過；兩份專項執行檔SHA一致。飲料發布證據的執行檔SHA負向測試被拒絕，未寫入 receipt。

入口檢查已共用化，依 receipt 驗證手機及飲料 manifest／曲線 SHA。舊版 receipt 沒有飲料欄位時維持正常啟動檢查；已執行原公開版本 ValidateOnly 通過。新版本發布會記錄 drink_manifest 與 SHA，防止資源改變卻沿用舊證據。

Runtime-StreetAmbientDrinkFull01 全部既有功能回歸 passed=true、errors=[]：15 一般話題、5 開場／5 道別、收集話題／觸控、名單／小地圖／導航／戀人呼叫／一對一、共同活動與設施及表情／取消恢復通過。獨立 848 音檔來源SHA、1230對話＋270觸碰0列對應錯誤、96張1920×1080 PNG解碼通過；戀人實際走來320.174公分。六人暖快取可見9.021秒，不代表冷載入或百人效能。

## 發布

同版 Runtime-StreetAmbientDrinkInvitations01 的20組實際語音接受／婉拒／取消與20次持久紀錄核對通過，獨立來源聲音及before／after存檔核對通過。

公開 StreetSocial.cmd 的 receipt 已更新至 StreetAmbientDrink01.stage，保留舊版備份。runtime_npc_phone／runtime_npc_drink／runtime_phone_use／runtime_invitation_ledger 皆 true，full_acceptance=false；手機／飲料共用 manifest SHA與曲線檢查及新版ValidateOnly通過。

當前通用生活活動功能已成功，不使用角色卡特化資源。這不是原作47行為與36場所全數完成；其餘活動、場所、完整施術／手機應用，以及最終GUI／畫面／聽音訊／長時間人工驗收仍列待辦。整合注意事項已寫入 STREET_SOCIAL_ERRORS.md。
