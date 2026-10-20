# 共同活動驗證

2026-10-08。

實作：通用同行狀態接入正式 StreetSocial scene，適用目前任意已載入女角。沿玩家軌跡追隨，保留每位角色自己的 APNG/runtime 骨架與動畫；靜態膠囊 sweep、NPC 距離阻擋，持續受阻/失效/超距則安全釋放。取消後保留當前位置並恢復日常 AI。

活動介面：搭話後選「一起散步／探索話題」，自動回鏡頭模式；WASD 帶路，F 拾取話題，Ctrl/中鍵游標，Esc/按鈕結束。同行期間不允許開新對話或設施，避免兩個活動爭用同一角色；結束後可重新搭話。

不建立卡片專屬 Cook 資產。不額外增加共用曲線、不新建全體 GPU pose bank、不捏造語音/獎勵。既有拾取依原有原子存檔流程；同行不改關係。雙人餐飲、合照與其餘配對動作/原作邀請分支尚未完成。

## 當前證據

- Python 30 項通過。
- Development StreetSharedWalk01：編譯成功，137.35 秒（包括此次共用 Rust 庫重建）。
- 場景回歸 Runtime-StreetSharedWalk01：passed=true、errors=[]、shared_walk_checked=true。已更新公開 receipt 與 ValidateOnly 啟動檢查。
- 新增 production-path probe：跟隨位移/實際提交、停步、NPC 阻擋、取消不瞬移、不改關係保存及重新進入。
- 完整人工 GUI、音訊聆聽與所有原作活動驗收仍未完成。

保留既有證據：正常語音15、好感正向提交12、話題提交4、觸碰3、設施3、原始語音雜湊823；獨立話題/語音驗證均通過。warm cache 六角可見14.25秒，不作冷啟動或性能承諾。實際 shared.walk.png 已檢視，男女均有行走姿勢。新 source/host cpp/h 位元組相同。


## 雙人合照擴充（驗證中）

來源：twoshot/list/00 的 MotionInfo。40 種原作性格恰好各一組，三組普通合照姿勢，男女四份共用曲線 634848 bytes。沒有卡片私有 Cook；每位 APNG 於 runtime 背景準備私有 GPU 動作。兩人均 ready 才同幀啟動，超時/失敗/取消同時恢復；截圖寫完前保留姿勢。

Python 33 項通過；Rust appearance::tests 7 項通過。PairedPhoto01 因兩個不支援的 rmpv 可變 API 編譯失敗，保留規則改為模式匹配後 PairedPhoto02 Development 編譯成功（50.79 秒）。Runtime-StreetPairedPhoto01 執行中，尚不更新公開 CMD。

共用動作清單上限從 8 擴到 32，數字鍵只讀現有 8 個鍵，避免陣列超界。GPU 動作準備仍為兩個 slot、queue8、4GiB；不是讓所有曲線都變成每位角色常駐 GPU bank。

完整原作身高混合/表情/手機道具、合照邀請與獎勵規則仍待對照；這輪使用非接觸姿勢、中身高來源曲線與角色私有體型，不擅自改好感。


Runtime-StreetPairedPhoto01：第一位合照與實際 PNG 成功；第二位發生 `Paired photo runtime prepare timeout`，驗證失敗，未發布。原因是共用 prewarm 對已安裝的相同 bank 回傳成功但未 pin，等待另一方時被回收。通用 API 現在會設定 StreamHold，由 play/cancel 釋放。PairedPhoto05 Development 成功（29.01 秒）；Runtime-StreetPairedPhoto02 重測中。照片目錄已改為存檔父目錄下 Photos，並新增拍照後立刻取消及滑鼠模式恢復檢查。


StreetPairedPhoto06 Development 成功（23.35 秒）。Runtime-StreetPhotoSmoke01 合照專用測試通過：五位不同 APNG、五張實際 PNG、準備排隊取消及立即重試、拍照後立即結束、雙方私有動作與有限骨骼、位置/旋轉/鏡頭/滑鼠模式及關係保存恢復。PNG 檔皆可解碼且 1920×1080。實際不同性格姿勢圖片已檢視；服裝既有破面/缺面仍屬完整視覺驗收待辦，不用功能成功取代視覺驗收。Runtime-StreetPairedPhoto03 正跑完整回歸，通過前不發布。


## 已發布的本輪功能

2026-10-08：StreetPairedPhoto06.stage / Runtime-StreetPairedPhoto03。passed=true、errors=[]、photo_only_test=false、paired_photo_checked=true、photo_actors_verified=5、photos_saved=5、photo_cancel_checked=true。五張實際 PNG 皆可解碼、1920×1080；先前 smoke 亦有五張與不同性格姿勢視覺檢查。

完整既有回歸：正常語音15、正向好感提交12、話題4、觸碰3、同行true、設施3；獨立來源驗證823音檔雜湊/15台詞及觸碰repeat[0,2,1]通過。Python33、Rust appearance7通過；Development23.35秒。六角 warm cache 可見11.31秒，非性能/冷啟動承諾。

公共 receipt 已更新；原 StreetSocial.cmd 不需改名。ValidateOnly、執行檔 SHA256、三份 cpp/h source-host 相同及 PNG 實際檔案檢查通過。full_acceptance=false；完整人工 GUI/聆聽、服裝既有破面、原作完整 height blend/表情/手機道具、邀請語音/獎勵與其餘活動仍待完成。

## 2026-10-08 男女坐談

- 最終來源：男角 studio/anime/00 的 m_suwari_00_03；女角 action/animator/00 的 f_suwari_00_00_M（Appearance3）。兩者是原作共用椅坐來源，搭配自建桌椅；不宣稱 action40/41 有男角分支。
- seated.manifest.json 兩個 common curve 合計 955,798 bytes。13個顯式共用活動 curve 可按需使用；每張 APNG 的 private rig/pose 仍在 runtime 背景處理，沒有新增卡片 Cook 資產。
- 共用 pair session 處理預約、兩方 ready 後同步開始、準備取消/重試、正在講話時離開、角色故障及恢復。離開清除場景桌椅，恢復雙方位置/旋轉、相機、滑鼠與原活動。
- 既有 voice/mouth/expression 和中文話題 UI 沿用；坐談採 voice-only 播放，完成語音、收集話題或觸碰反應時不觸發站立 gesture，也不解除坐姿。正常原作話題效果仍走既有完成音訊才提交的規則。活動本身不製造好感獎勵。
- 坐談鏡頭取男女 private head world positions，不能用站立時固定135cm高度。
- StreetSeated05 Development 編譯成功 17.79 秒；Python34通過。root/isolated host 的 Scene.cpp、Scene.h、RuntimeStream.inl SHA逐一一致。
- 專用 Runtime-StreetSeatedSmoke04：passed=true/errors=[]，5名女角各自 private seated pose、5段來源 greeting 語音完成、準備取消/重試及 active speech cancel/restore 通過；5張PNG均可解碼為1920×1080，已查看第1/3/5人的座位與構圖。
- 本次 focused warm-visible 8.588秒；不當作冷載入或性能驗收成績。

失敗紀錄：Smoke01為33秒動作超128MiB預配置；Smoke02/03流程通過但來源坐姿類型不匹配，最終以女角椅坐替換。詳見 STREET_SOCIAL_ERRORS.md。原33秒共用曲線保留作來源研究，未列入正式活動manifest。

邊界：目前驗證坐談語音與取消，完整人工UI/聽音訊/所有中間動作及不同身高座位精修留最後。源動畫沒有經裁短、動畫頻率維持60Hz；長按需動作允許512MiB keys/768MiB bank組裝上限，兩準備slot及4GiB residency限制不變。完整原作邀請/接受/拒絕、height blend、共餐與配對遊戲仍待完成。

Full01 追加發現當日 greeting 已不 eligible 時的重複招呼問題，未發布。06版入座不再強制招呼；probe 明確使用 fresh relation 的 source-matched greeting fixture（同 production QueueNode/FinishLine）驗證語音，不放寬正常玩家條件。Full02回歸後才能發布。


最終發布：StreetSeated06.stage / Runtime-StreetSeatedFull02。Development成功21.21秒；passed=true/errors=[]；15正常話題、5正常招呼/5道別、12次正好感提交、4次收集話題、3次觸碰、3設施、同行、5次真實合照、5人坐談語音fixture完成/queued取消重試/active取消與restore全部通過。坐談入座不再強制重複招呼。

獨立核對：823來源voice hashes、15 voiced topics matched、touch repeats [0,2,1]均通過；5張本輪坐談PNG可解碼為1920×1080。Scene.cpp/h與RuntimeStream.inl的source/host bytes一致。發布器更新receipt並通過正式launcher ValidateOnly；百人CMD未改。warm all-visible 9.104秒，未作冷載入/FPS驗收。

使用入口仍是 StreetSocial.cmd；先E交談，待目前語音結束，按「坐下聊天」。入座後選原有話題或互動；Esc/「起身離開座位」取消並恢復。原作完整邀請分支、height blend、雙人餐飲/遊戲及最後人工驗收仍未完成。

## 共同飲料：來源與第一批場景檢查

2026-10-08。Source Studio info 證明 ks_m_action_02 是自販機蹲下動作、f_tachi_00_09 是等待；均不冒充飲用。實際使用 action/animator/00.unity3d|f_tachi_00_10 共用曲線（305035 bytes），男女從各自 APNG 背景生成私有 GPU 動作。不是原作男角專用飲用動畫，沒有卡片專用 Cook 資產。

StreetDrink06 Development 19.58秒，Runtime-StreetDrinkSmoke03 五位女性皆通過雙方 clip、每幀手部掛點/道具、兩個不同時間點動作變化、準備中取消再進入、離開完整恢復與關係存檔不變。2520筆 hand samples，10張1920x1080 PNG；已查看1/3/5號，男角不再蹲下。杯瓶以既有共用 Cylinder 自製，按原瓶底掛點更新，未使用卡片專屬資源。

此 smoke 尚不作為發布證據；補驗活動中語音完成與語音中止後，再跑完整回歸。原始誤判/編譯錯誤/錯誤斷言皆在 STREET_SOCIAL_ERRORS.md。


飲料補驗已通過：StreetDrink08 / Runtime-StreetDrinkSmoke05，五人各一段來源招呼語音 fixture 完成，語音不覆蓋 drink pose；第二段實際播放中取消，雙方/相機/滑鼠/保存復原；5472筆手部道具樣本、10張PNG。Runtime-StreetDrinkFull01 原有15正常話題/5招呼/5道別/4話題交易/3觸碰/3設施/同行/5合照/5坐談與5飲料皆通過，errors=[]。因新增書本，尚未以該舊build發布。

閱讀補驗：StreetBook02 Development19.09秒，Runtime-StreetBookSmoke02 passed=true/errors=[]，男角+五位女角各自確認 private reading pose/右手掛點/五個書本部件實際顯示，切換 walk_01 後全隱藏，恢復活動且存檔不變。六張PNG1920x1080均可解碼，已看0/1號實際畫面；warm all-visible 約9秒（詳細數值以report為準），不是冷啟動/FPS驗收。Scene.cpp/h、RuntimeScene.cpp/h、RuntimeStream.inl 來源與host bytes一致。


## 最終發布：書本道具與共同飲品

2026-10-08 StreetBook02.stage / Runtime-StreetBookFull01，passed=true、errors=[]。35項Python、Development19.09秒、實際6名runtime APNG GPU角色通過。六人閱讀各自右手書本與轉走路隱藏通過；五人共同飲品各自姿勢/跟手道具/來源語音完成/準備取消重試/正在播放的語音取消/雙方相機滑鼠保存復原皆通過。

完整回歸：15正常話題、5招呼/5道別、12正好感、4收集話題交易、3觸碰、3設施、同行、5普通合照、5坐談語音及上述閱讀/飲料均通過。獨立核對823來源音檔雜湊、15正常有聲話題matched、touch repeats[0,2,1]通過。六張閱讀+十張飲料PNG均為1920×1080可解碼，root/host五份改動相關source bytes相同，exe SHA及正式launcher ValidateOnly通過。

已將原 StreetSocial.cmd 的 receipt 指向新版。warm all-visible 11.675秒，非冷載入或FPS驗收。百人CMD未改；使用者關係存檔未被測試覆寫，selftest使用自己的relationships。完整人工驗收/完整原作邀請、height blend、雙人用餐/其他配對遊戲仍待完成，full_acceptance=false。


## 雙向潑水：本輪功能測試

StreetSplash02 Development18.52秒，Python36項通過。來源 studio/info/00.unity3d Anime_06_11_10 summer 控制器：男 m_action_02/m_action_03、女 f_action_00 [summer]/f_action_01 [summer]，四條共用曲線1,465,144 bytes，manifest保存來源bundle/curve SHA及女性唯一pathID，從來源讀出共同週期3.333333秒。曲線無AnimationEvent，水花相位與配置為本作自製。

Runtime-StreetSplashSmoke02 passed=true/errors=[]，六名runtime APNG GPU角色，五女孩各兩方向共10案例；準備中取消/重試、第一與第二來源循環水花、各自private clip/有效head/hand、活動取消、雙方位置/旋轉/held、相機/滑鼠及存檔不變通過。52,759筆水滴更新，20張1920×1080 PNG可解碼，已檢查男性潑水與女性第二循環實際截圖。warm全員顯示8.491秒，不是冷啟動/FPS驗收。

完整功能回歸 Runtime-StreetSplashFull01 正在執行；本節 smoke 不作為公開CMD發布證據。完整人工驗收仍留最後。


## 發布：雙向潑水

2026-10-08 StreetSplash02.stage / Runtime-StreetSplashFull01，passed=true/errors=[]。Development18.52秒，Python36項通過；獨立核對823來源音檔雜湊、15有聲話題matched、12正好感提交、touch repeats[0,2,1]通過。完整回歸包含15正常話題、5招呼/5道別、4收集話題交易、3觸碰、3設施、同行、5合照、5坐談、6閱讀、5飲料語音及10潑水角色分工；54,519筆水滴更新。全部48張1920×1080 PNG可解碼，root/host相關五份source bytes一致。暖快取全員顯示8.531秒，並非冷載入或FPS驗收。

原 StreetSocial.cmd receipt已切換 StreetSplash02，exe SHA/正式launcher ValidateOnly通過。百人房間入口未改，原Editor未關閉/Live Coding，測試使用獨立關係存檔。完整人工驗收仍留最後，full_acceptance=false。雙人用餐、打西瓜、購物/完整世界設施及原作邀請/身高混合仍未全部完成；潑水水花是自製效果，沒有挪用語音/捏造好感獎勵。

StreetMeal01 / StreetMealSmoke01：Development20.06秒、五女角共餐專用實景probe通過，男女私有進食clip、右手/湯匙移動、有聲fixture、準備取消/重試、語音中取消/存檔恢復通過。已查看實際畫面，男角重定向進食及男女入座合理；餐盤初版為木色且未放食物，下一版補明顯餐盤/食物後再測。未發布01。

## 雙人用餐最終發布（2026-10-08）
StreetMeal03.stage / Runtime-StreetMealFull02：passed=true、errors=[]。Development20.70秒，36項Python測試通過。
- 五名女角逐一與男主共同用餐；各自APNG背景產生私有GPU姿勢，均播放共用 facility_eat.okanim（原作 f_eat_00_00_M，5.3秒，SHA256 9c119ca1ad6a307574c7eab0ee4e333b5e0fe9b8b7ba9c9d3ebb0575fec02541）。男角是共用曲線重定向，未宣稱原作男角專屬動畫。
- 27個自製共用桌椅/餐盤/食物/湯匙部件完整建立後開始。7,797筆手部檢查，兩人湯匙的兩部分都跟隨各自右手；五名女角語音fixture完成、準備取消/重試、語音中取消、人物/位置/鏡頭/游標/存檔恢復通過。
- 原15正常有聲話題、5招呼/5道別、4收集話題、3觸控、3設施、同行、5合照、5坐談、6閱讀、5飲品、10潑水與小地圖回歸通過。獨立823來源音檔hash、15話題匹配、12正好感交易、touch repeats[0,2,1]通過。
- 60張截圖均可解碼為1920×1080，實際查看女孩1/5用餐圖片、男角姿勢、淺色餐盤/食物與餐具。source/host六檔一致。公開StreetSocial.cmd已更新，ValidateOnly通過。
- 暖快取全員顯示8.541秒；非冷啟動/FPS驗收。原Editor未關閉或Live Coding，百人CMD未更動，測試使用自己的存檔。
- Full01因材質查表修正中止，不作發布證據。完整人工驗收仍留最後，full_acceptance=false。打西瓜、購物/完整世界設施、原作完整邀請/height blend仍待後續。


2026-10-08 打西瓜最終發布 StreetWatermelon08.stage / Runtime-StreetWatermelonFull01。十組真實命中、37,744掛點檢查、準備／活動取消還原及既有完整回歸通過，errors=[]；38 Python／原生接觸2＋軌跡1／獨立823來源音檔hash與15話題配音／80PNG通過。原CMD receipt及ValidateOnly通過。新場景80cm配置配合通用背景接觸校正，不捏造原作IK、男主專屬動畫、邀請配音或額外好感。最新證據及限制見 STREET_SOCIAL_WATERMELON_VALIDATION.md；本節前面的「打西瓜待做」為舊版歷史狀態，完整人工與其餘世界內容仍待完成。
