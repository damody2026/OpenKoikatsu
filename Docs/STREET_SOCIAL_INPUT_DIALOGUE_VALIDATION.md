# 街區朝向、滑鼠模式及男主台詞驗證

2026-10-08，StreetInput02。主代理實作，功能優先。

## 修正

- APNG 模型正面是 local +Y，移動向量的 Rotation 朝向是 +X。統一透過 OKStreetFacingMovement 轉換減 90 度，男主、五名 NPC、交談轉身一致處理，停止不改朝向；不依卡片名稱特判。
- 預設永久滑鼠捕捉、視窗內鎖定、隱藏游標、直接轉鏡頭；Ctrl 或中鍵切換 UI 模式，解除捕捉並顯示游標。UI 模式停止男主移動及鏡頭 delta。以 OS 視窗前景狀態判斷切窗，返回恢復原模式、略過恢復幀 delta；Ctrl／中鍵經 Slate preprocessor，UI 按鈕取得焦點也能切回。
- 女角語音字幕前加角色名，新增獨立男主台詞欄。男主開場、日常、戀愛、親密及道別是明確的場景本地文字，未聲稱原 KKS 台詞，也沒有新造男主語音。

## 已完成證據

- StreetSocialHost-StreetInput02.log：獨立 Development 編譯成功。
- Runtime-StreetInput02/social.validation.json：passed=true，errors=[]，mouse_mode_checked=true，runtime_facing_checks=10，player_prompts_shown=32。
- 五方向男主提交矩陣與五名 NPC 的真實移動提交回讀：local +Y 與移動方向 dot>0.999。
- 四種模式的真實控制器／viewport 設定檢查：UI 釋放、遊戲捕捉、失焦釋放、恢復鎖定。
- 六 APNG：15 話題、5 招呼、5 道別，10 次正好感提交與保存一致；動作、嘴型、表情、陰影、取消／恢復斷言通過。
- 15 話題的 APNG 身分／性格／語音序列獨立比對通過。
- Python 26 項通過。
- 實際 1920x1080 street.dialogue.png 已開圖檢查：男女兩欄台詞可讀，兩角色面對面。不是整套人工操作／聽音驗收。

## 發佈與驗收界線

- 隔離 Editor 原生 14 項全部通過（新增 MovementFacingAndPlayerPrompts），零 failed／notRun。使用引擎正式 -UsePrecompiled 路徑，不干擾已開著的主 Editor。
- 原本 StreetSocial.cmd 已經透過 receipt 切到 StreetInput02.stage；ValidateOnly 與獨立 executable SHA256 比對通過。
- 執行檔 SHA256：`3aacd169acb8f78b8a945fd71612e9f430b81ded2874f1dc8773e2e4d32cb8f4`。
- OS Alt+Tab、Ctrl／中鍵實際手感及 UI 點選的完整人工操作仍依指示留到最後；自動設定斷言不冒充實際桌面輸入驗收。
- 主專案已開著的 Editor 沒有熱更新；公開 CMD 使用獨立 Development。

增量 diff 位於 Saved/StreetSocial/InputReview/implementation.diff；改動前源碼保存在 before。所有失敗嘗試寫入 STREET_SOCIAL_ERRORS.md。
