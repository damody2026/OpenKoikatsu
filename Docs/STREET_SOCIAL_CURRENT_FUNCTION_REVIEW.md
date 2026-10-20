# 最新功能版本核對

2026-10-08，主代理直接核對。

- 公開 StreetSocial.cmd 仍指向 Tools/run_street_social_development.ps1；ValidateOnly 通過，receipt 使用 StreetVoicedPreview01.stage，執行檔 SHA256 重新核對一致：3b8bd22a8d978bd6fccb4f3f1e279c7e79bd19c5b9f819c427d156088213c11c。
- 重新讀取實際 social.validation.json、voice.reward.validation.json 與原生 Automation-StreetVoicedPreview01：場景功能及語音匹配 passed=true，errors=[]，13項原生全部通過。這是核對既有已完成測試，不宣称本輪重新跑了遊戲或編譯。
- 實際開啟檢視最新 street.dialogue.png／street.selftest.png：可见男角、交談女角、街區、字幕與話題UI，未见此前的爆炸頂點或整張臉缺失。這兩張固定角度截圖不能代表所有人物、全部動畫、操作或人工聽音訊驗收。
- 未發現新的核心功能缺漏，本輪不再增加範圍或重複編譯；第一版前五項功能完成狀態維持。錯誤處理歷史保存在 STREET_SOCIAL_ERRORS.md，本輪沒有新增執行錯誤。
- 場景目前仍是自製demo規模；截圖远方有黑色地平線、建築以基本幾何呈現，不能把功能通過當成美術完整驗收。正式視窗操作、各角度角色外觀、人工聽音訊、全部內容及長時間驗收依使用者指示留到最後；full_acceptance=false。

後續修正：此記錄中的黑色地平線已於 StreetBackdrop02 補上通用runtime遠景地表／薄霧，實際新截圖及功能回歸通過；見 STREET_SOCIAL_BACKDROP_VALIDATION.md。原始截圖與本核對紀錄保留為歷史證據。
