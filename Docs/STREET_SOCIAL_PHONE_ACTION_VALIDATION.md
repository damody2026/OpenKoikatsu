# 站立手機活動驗證

## 實作範圍
右側共同活動新增「一起使用手機」／「結束手機活動」。男女各自以 APNG 在 runtime 背景準備站立 Phone 原曲線，不共用另一人的 pose，不建立卡片專屬 Cook。手機外殼／螢幕／鏡頭為自製共用幾何，尺寸依原道具bounds，掛點依 EasyLoader type8 使用本人的 k_f_handL_00。

使用既有兩人預約／準備／播放／退出恢復機制，60秒準備上限、可取消。可沿用既有交談，語音不覆蓋手機姿勢；沒有新增虛構手機語音、好感獎勵或存檔效果。不宣稱原作男角專用手機動畫、坐姿手機遊戲、完整手機介面或自動NPC手機行為排程已完成。

## 當前功能證據
- Development StreetPhoneUse01 編譯成功54.08秒，現用場景／手機／邀請／規則六份來源與host雜湊一致。
- 47項Python測試通過，包括原 Phone 狀態來源曲線、WaitPoint左手道具規則、OKCLIP格式及來源／曲線SHA。
- Runtime-StreetPhoneUseSmoke01：passed=true、errors=[]，六APNG GPU就緒、無私人Cook；五女角各與男主播放原曲線、手部道具追隨、多時間點動作變化、語音完成／播放中取消、準備取消重試、人物／相機／游標／活動恢復及存檔不變全部通過。
- 專項原生 phone_use_samples、語音及私有掛點斷言；獨立腳本核對六名actor均實際安裝phone_standing.okanim、共用曲線來源／SHA及十張1920×1080截圖。已實際檢視 street.phone-use.1.first.png，男女手機均正常握持。
- 共用新增曲線349,939 bytes；私有GPU資料依請求在runtime有界準備，初始npc_action_preload_count=0。

Runtime-StreetPhoneUseFull01 整合回歸 passed=true、errors=[]：手機五人／5段完成語音／7,996個手持更新樣本、原有15一般話題／5開場／5道別、4收集話題交易／3觸碰、五位戀人私有模式／導航／電話實際走動320.69cm、合照／坐談／飲品／閱讀／同行、10組潑水、10組打西瓜與10次命中及三設施生命週期全部通過。獨立來源核對848音檔SHA、五位戀人語音及手機共用曲線／六人私有安裝；96張1920×1080PNG解碼通過，實際檢視street.phone-use.5.second.png。

Runtime-StreetPhoneUseInvitations01 同一執行檔再驗20組原邀請／20取消／20持久ledger及獨立前後存檔與語音來源通過。公開 StreetSocial.cmd 已切至 StreetPhoneUse01.stage，沿用 StreetQueueReuse01.patch、RuntimeV3；receipt 原子發布、執行檔SHA及 ValidateOnly 通過，runtime_phone_use=true、runtime_invitation_ledger=true、full_acceptance=false。

六人暖快取全部出現12.612秒（手機專項8.757秒）；這是六人功能測試，不能當百人性能或冷啟動驗收。主Editor未停止、未Live Coding，使用者正式關係存檔未作fixture。完整人工操作／聽音及世界內容驗收留最後；性能依使用者要求不在本輪驗收。
