# 自主手機行為驗證

實作：現有 Rest／Look 停留節點可進入自主使用手機，公平輪替、每人 45 秒冷卻；最多一名背景準備／播放，60 秒準備上限、播放 8 秒後繼續原路線。這些時間是本專案的排程設定，不宣稱完全等同原作 AI。

原作共用動作 `f_tachi_00_04` 與左手 `k_f_handL_00`；私人姿勢由任意 APNG 骨架 runtime 產生，沒有角色專用 Cook 資料，沒有新增啟動預載。

前景對話、雙人活動、陪走、設施、戀人呼叫及一對一互動可以取得優先權；背景手機移除後才建立前景預約。自然結束與取消不修改好感／交談額度。

Development 已編譯成功，47 項 Python 測試通過。獨立五人場景驗證與既有功能回歸進行中，完成後更新證據。最終人工驗收仍待進行。

初版專項 Runtime-StreetNpcPhoneSmoke01：passed=true、errors=[]；五人準備取消、五人對話中斷、五人自然結束並位移恢復，12844 次手機道具更新；獨立五人串流日誌與五張 1920×1080 PNG 核對通過。角色1截圖已人工確認手持手機。六人暖快取可見 9.491 秒，不代表冷載入或百人效能。

整合時額外修正名單戀人電話按鈕：允許自主手機擁有者被呼叫，其他前景保留仍不可點。StreetNpcPhoneFull01 在完成前由主代理精確核對本次 PID／exe 後中止，標記 aborted，不計成功；將以 StreetNpcPhone02 新版專項及回歸作為發布依據。

修正後 StreetNpcPhone02 建置成功（31.68 秒）。Runtime-StreetNpcPhoneSmoke02 五人專項及獨立核對再次全部通過：9879 次道具更新，五人準備取消、五人對話中斷、五人自然結束後行走、errors=[]。角色1畫面確認手機在手；角色5被男主部分遮住，僅記錄數值驗證通過，不宣稱五人畫面全部人工驗收。六人暖快取可見 14.120 秒。

發布門檻另驗：自主專項從實際 game.log 的 Base Directory 取得測試執行檔並記錄 SHA256。將證據雜湊改成錯誤值的負向測試被拒絕，未寫入 receipt；最終發布須符合實際測試的相同執行檔，不能只比對路徑。

新版 Runtime-StreetNpcPhoneFull02 全部既有功能回歸 passed=true、errors=[]：15 一般話題、5 開場／5 道別、收集話題／觸控交易、名單／小地圖／導航／戀人呼叫／一對一、共同活動／設施、私人表情與恢復均通過。原作來源語音 848 個 SHA、1230 對話＋270 觸碰的 0 列錯誤、96 張 1920×1080 PNG 解碼通過；戀人實際走來 320.211 公分。共同手機角色5第二張已人工確認雙方各自手持手機。六人暖快取可見 10.153 秒。

## 發布結果

同一 StreetNpcPhone02 執行檔 Runtime-StreetNpcPhoneInvitations02：20 組接受／婉拒／取消語音與 20 次持久紀錄核對通過，獨立 before／after 與來源聲音比對通過。

receipt 已原子更新並保留舊版備份：`runtime_npc_phone=true`、`runtime_phone_use=true`、`runtime_invitation_ledger=true`、`full_acceptance=false`。`StreetSocial.cmd` 沿用原入口，現在啟動 StreetNpcPhone02.stage；ValidateOnly 通過。

錯誤及修正記在 STREET_SOCIAL_ERRORS.md。這輪只完成自主站立手機排程；坐姿手機遊戲、完整手機應用介面、其餘世界活動／場所與最終人工驗收仍未完成。沒有重新宣稱性能目標或百人冷載入已通過。
