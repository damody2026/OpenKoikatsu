# 對話 UI、站姿與跑步驗證

日期：2026-10-08。Development 候選：`StreetPresentation05.stage`。

## 本輪行為

- 一般交談時男女使用站立待機，女角依個性分成內斂、開朗及一般站姿；語音手勢結束後回到站姿，沒有手持書本。
- 右側「拋出話題」收合／展開話題。選項與中下方對話框使用半透明圓角面板。
- 底部六個數值滑桿直接在 OnValueChanged 原子保存並更新目前資料；只改單一欄位，不使用整份舊草稿覆蓋其他互動。鍵盤調整與拖曳使用同一路徑，不需保存按鈕。
- 滑桿與標籤持續綁定最新關係資料；Slate 下一個畫面更新滑桿位置，上方數值摘要同樣綁定最新資料。
- Shift 跑步使用男角跑步動畫（420 cm/s）；普通巡路女角偶爾跑 2.5～5 秒，使用女角跑步動畫（270 cm/s）。停留、交談及活動時停止。

## 已通過的專項

- `Runtime-StreetPresentation05b`：5 名女角交談站姿、男女無書本、6 人跑步動畫、話題收合／展開、六欄保存重載及外部數值變更回寫滑桿、對話面板位置不與右側重疊。
- `Runtime-StreetPresentation05Movement`：裝飾鋪面與牆體碰撞、5 名女角限速轉向、滑鼠係數維持原設定。
- 53 項 Python 測試通過，包含個性站姿映射、來源資料及發布證據檢查。
- 共用 `locomotion.manifest.json` 六個來源曲線雜湊一致；每個 APNG 在 runtime 建立自己的 GPU 姿勢。

## 既有功能回歸

- `Runtime-StreetPresentation05Full`：完整原生回歸通過，15 段對話、94 次來源表情、5 名女角導航／電話／一對一、共同散步、三設施、合照、坐談、閱讀、手機、飲料、潑水、用餐與10組打西瓜接觸及取消還原；errors=[]。
- `Runtime-StreetPresentation05AutoPhone/AutoDrink/AutoSelfie/AutoGroom`：四種自主活動各5人，準備取消、交談中斷、正常完成與獨立來源核對全部通過。
- `Runtime-StreetPresentation05Invitations`：邀請回應、取消與每日保存紀錄及獨立來源核對通過。
- 既有 contacts/topics/phone/source-layout 獨立核對通過。
- 已人工檢視 `standing.1.png` 與實際語音 `street.dialogue.png`：右側選項、右下數值、下方對話框、無書本與半透明面板正常。
- 發布後 `run_street_social_development.ps1 -ValidateOnly` 通過；原 `StreetSocial.cmd` 透過 receipt 載入 StreetPresentation05，啟動時核對共用站姿／跑步資源雜湊。舊 receipt 已自動備份。

此紀錄包含本輪專項實景檢查與自動回歸；既有整個專案的最後完整人工驗收仍依原檢查表另行記錄。
