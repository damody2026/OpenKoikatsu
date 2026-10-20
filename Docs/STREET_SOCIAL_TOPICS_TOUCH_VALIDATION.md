# 話題與交談觸控驗證

2026-10-08。主代理自行完成；完整人工驗收留最後，這輪僅驗證當前功能。性能驗收依使用者指示暫緩。

## 實作

- 48 種共同話題：每日街上拾取、持有上限 99、話題袋、女生出題、精確／同類／錯誤與敷衍回答、稀有使用紀錄、興趣加成、情色話題興奮／慾望、原子庫存與關係保存。
- 補交談掉落 TopicDrop_00 的 48 筆規則，使用原作 probability，戀愛／情色完成次數加成 +2。Generic 的話題編號取同一 personality、stage、command、container 的 talk[0]，不漏掉沒有語音的男主行。話題掉落與關係效果同一原子存檔，不會消耗每日拾取資格。
- Ctrl／中鍵顯示游標後，可點選交談女角的頭、臉頰、左右手、左右胸；亦提供部位按鈕與觸碰／注視切換。只有交談對象可以互動，注視模式不支持手部。
- 原作 TouchFunc 的部位映射、左右共用次數、反應 index 0／2／1、關係 stage 0／1／2，以共同表格處理。反應使用匹配的原作語音、文字與表情，嘴型沿音訊時鐘。
- 觸碰會打斷原語音及動作 epoch，取消未提交的話題／關係预覽。反應播放完成後才唯一提交來源好感／興奮效果與一次交談成本；取消、來源缺音、播放或存檔失敗不提交。
- GPU 動畫的互動位置查詢使用私人 clip 及當前 blend，僅在點擊時求所需骨骼祖先鏈。ExternalControl render rig 保留存在的語義骨骼；cache recipe 包含骨架，因此不會沿用錯誤的舊骨骼索引。卡片不用新增專用 Cook 資產。

## 來源限制

五種測試性格每種 54 筆 Touch／See 定義，53 筆可用，共 823 個話題／互動共同音檔。原作抽取資料每種都缺 stage2 Touch part3 repeat2 的 com_ev_XX_395：戀人階段第二次觸碰臉頰。這筆操作會顯示獨立缺音提示，不播放或扣交談額度，僅前進本次交談部位次數，下一次依第三次反應播放；沒有借用其他聲音。

本次提供街區的完整話題遊玩循環與交談互動，不宣稱復刻原作所有世界設施、祈禱加成、完整 ADV 劇情或每行 pose／gaze IK。APNG 沒有原作 Heroine.isVirgin，不能推測該旗標。部位球形拾取區與桌面鼠標／實際觸控手感留待最後人工驗收。

## 測試紀錄

- Python 26 項現有測試及工具語法檢查通過。
- Rust editor_mods 的保留其他 plugin／座標資料測試通過（1 項），僅修不存在的 rmpv mutable API 編譯阻擋。
- StreetTouch06 Development 編譯通過，50.63 秒；Runtime-StreetTouch02 抓到稀疏骨骼被 render rig 裁掉，沒有發佈。
- StreetTouch07 Development 編譯通過，25.32 秒。Runtime-StreetTouch03 第一筆觸碰語音與效果通過，後續因隔離測試先耗掉交談額度而被正式門檻拒絕；只補測試fixture額度，未放寬正式門檻。
- 最新 StreetTouch08 Development 編譯通過（24.66 秒）；Runtime-StreetTouch04 `passed=true / errors=[] / full_acceptance=false`。正常15有聲話題、5招呼、5道別、4話題交易、3觸碰交易、取消與回讀通過。
- 觸碰共用左右手計數，實際反應順序 0／2／1，來源好感分別 25→28、28→28、28→28；原作沒有設定增加的行維持0，不人工加分。打斷的話題庫存仍為1，取消的頭部反應沒有提交。
- 隔離原生 Automation-StreetTouch02：16 過、0 失敗、0 未執行。涵蓋部位／重複映射、53可用行及缺音拒絕、來源掉落機率邊界、+2加成、99上限、沒有語音男主行的掉落編號、庫存與每日拾取互不干擾。StreetTouch08 只改測試fixture額度，規則與此原生證據相同。
- `collected-topics.independent.validation.json`：823 個來源音檔 hash、4題目結果與3觸碰效果／語音順序、7筆存檔交易獨立核對通過；正常15話題語音也獨立全部匹配，12筆好感提高。
- 六人全部可見時間8.83秒（已存在GPU cache，不能稱全冷啟動或百人效能）。實際截圖 `Saved/StreetSocial/Runtime-StreetTouch04/street.collected-topics.png` 已檢視，互動按鈕與男女字幕可見。
- Runtime-Runtime-StreetTouch01：測試播放守衛造成等待卡住，已修正並終止本任務的測試程序。Automation-StreetTouch01：15 過／1 失敗，原因是缺音種類的測試假定不正確；已改用真實來源行確認。上述失敗保留，不當成通過證據。

公共入口 `StreetSocial.cmd` 已以原子 receipt 指向 StreetTouch08.stage；ValidateOnly／executable hash 驗證通過。百人 CMD 保持原入口；使用者 relationships.json 沒有拿來當測試fixture。原專案正在執行的 Editor 不關閉、不熱換 DLL。

## 缺音後恢復：StreetTouch09（2026-10-08）

- 直接檢查五個性格原始 adv 全部 bundle 的 AudioClip 名稱，395 都不存在，394／396 存在。可重現工具 Tools/verify_street_touch_source_gaps.py；原始 bundle SHA256 與證據 Saved/StreetSocial/Build/touch-source-gaps.json。無語音替換、沒有借用其它性格或文字不符的語音。
- 共同表格保留全部54筆 Touch／See 元資料、53筆可播放語音。只有明確 available=false 的反應會前進本次交談部位次數，顯示提示但不停止舊對話、不建立交易、不消耗額度、好感或興奮。執行期索引／準備失敗不能被當成來源缺音。下一次依原始第三次反應播放；计數飽和於3。
- Python26、native16／0失敗通過（Automation-StreetTouch03）；Development StreetTouch09 build成功。隔離 Editor 的相對路徑失敗保留 StreetTouchEditor03.log，绝對路徑 StreetTouchEditor04.log 成功。
- Runtime-StreetTouch05 正式六人場景 passed=true、errors=[]、touch_missing_recovery_checked=true。15段正常對話、12次好感提高、4筆話題交易、3筆觸碰交易、反應0／2／1均通過。隔離fixture模擬戀人第二次缺音後第三次可播放，隨即取消，記憶體效果與磁碟存檔均未提交；測試沒有動使用者 relationships.json。
- 獨立驗證823個來源音檔hash、7筆存檔交易、15段正常語音吻合。六人顯示10.62秒，使用既有cache且有並行編譯，不作冷載入／性能結論。已檢視當輪實際 street.collected-topics.png，僅作UI與場景功能確認。
- 公開 StreetSocial.cmd 經原子 receipt 更新到 StreetTouch09.stage；ValidateOnly、executable SHA256、root／host關鍵來源一致驗證通過。完整人工滑鼠互動、聽音、美術與長時間驗收仍留最後，full_acceptance=false。

## 輸入與字幕分流：StreetTouch10（2026-10-08）

- Slate 輸入預處理器於左鍵按下時讀取實際交談面板 cached geometry，記錄 UI 命中；場景點選不能再使用同一次 UI 點擊。位置使用 Slate 絕對座標，沒有固定螢幕矩形或卡片特判。背景視窗、鏡頭模式、沒有交談對象、已有觸碰反應也拒絕場景點選。
- 觸碰拒絕／缺音通知使用 TouchNotice 獨立顯示，不改 S.Message / PlayerLine。成功觸碰及結束交談清除通知，不讓技術提示冒充女角台詞。
- Development StreetTouch10 編譯成功33.69秒。Python26及工具語法檢查通過；隔離Editor StreetTouchEditor05成功，Automation-StreetTouch04共16通過、0失敗／未執行，包含全部場景點選門檻。
- 正式 Runtime-StreetTouch06 passed=true、errors=[]：touch_input_routing_checked=true，實際面板幾何內／外點使用正式回呼驗證；touch_missing_subtitle_checked=true，在實際有嘴型／語音的話題中觸發原始缺音，男女字幕／Node／VoiceIds／舊話題預留／存檔均不變；缺音後第三次恢復及取消仍通過。這是輸入路徑與真實幾何測試，沒有冒充人工OS滑鼠點擊驗收。
- 原有15有聲台詞、5招呼／5道別、4筆話題與3筆觸碰交易保持通過。獨立823個來源音檔hash、7筆存檔交易、正常15台詞／12筆正好感全吻合。實際 street.collected-topics.png 已檢視；六人9.12秒使用既有cache，不作性能驗收。
- 公開 StreetSocial.cmd 指向 StreetTouch10.stage，ValidateOnly、獨立executable SHA256及root／host五個關鍵來源一致。操作指南同步Ctrl／中鍵鏡頭、F拾取、女生出題、話題消耗／取消、觸碰／注視與缺音行為。完整人工視窗、鼠標／觸控手感、聽音／內容及長時間驗收仍留最後。

## 等待答案與交談操作：StreetTouch11（2026-10-08）

- 語音／未完成話題（包括等待答案與無聲男主行）／關係預覽／觸碰反應共同定義 Busy。語言按鈕與 ToggleLanguage 函式都使用此判斷，不允許在交易中換 catalogue 或蓋掉題目提示。
- 結束按鈕與 Farewell 共用相同 Busy：忙碌時顯示／執行取消，純待機才顯示／播放道別。取消清除題目等待、預留與動作，不消耗未完成的話題或提交關係效果。
- L／1～4／N 的正式 Tick 路徑補前景 Focused 條件，UI游標模式仍可使用；原移動／搭話／Esc及Ctrl／中鍵行為保留。此條件完成程式審查，不冒稱已做人工背景視窗按鍵驗收。
- Development StreetTouch11成功48.38秒；Python26及工具語法檢查通過。隔離Editor StreetTouchEditor06成功107.11秒，Automation-StreetTouch05共16通過、0失敗／未執行；包括等待答案、無聲台詞、關係預覽與觸碰準備狀態。
- Runtime-StreetTouch07 passed=true、errors=[]、conversation_controls_checked=true：實際女孩語音出題後等待回答，ToggleLanguage沒有更改語言、男女提示、等待與庫存／關係；使用正式 Farewell 取消入口清除題目與預留，完整Store序列化保持不變，隨後再次BeginTalk成功。其餘UI／場景指標與缺音字幕／第三次恢復／取消全部通過。
- 原有15正常有聲話題／12筆正好感、5招呼／5道別、4話題交易、3觸碰交易保持通過；獨立823個來源音檔hash、7筆存檔交易與全部15語音匹配。六人13.78秒使用既有cache且有其他程序／並行build，不作性能或全冷載入結論。
- 公開 StreetSocial.cmd 更新至StreetTouch11.stage，ValidateOnly與獨立executable SHA256通過；root／host五個關鍵來源一致。操作指南與錯誤紀錄同步，百人CMD與正在執行的主Editor不動。完整人工視窗／輸入／聽音／內容覆蓋與長時間驗收仍留最後。
