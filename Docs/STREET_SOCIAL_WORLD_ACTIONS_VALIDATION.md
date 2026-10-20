# 場所／動作／繁體中文驗證

2026-10-08。完整世界與人工驗收尚未完成；不以來源清單冒充可玩內容。

## 已完成的共用資料

- world.json：原作 36 個場所、47 個行為、420 筆 WaitPoint、214 個 base Animator 狀態，含來源雜湊、原始姿勢／道具／偏移及六個嵌入設施的父場所。
- zh_tw.json：282 種精確補譯／修正，48 個話題名稱中文化；現有五種性格 1,230 筆一般節點及 1,680 段話題／觸碰台詞全部非空字幕通過覆蓋檢查。空白控制節點不捏造台詞。
- 中文在 prepare_street_social_selection.py 準備 APNG 選擇時套用，不改原始 catalogue、台詞順序、聲音、分支或交易。原日文及 NameJp 保留。
- facilities.manifest.json：三份來源明確的共用曲線，合計 868,229 bytes（0.83 MiB）。各 APNG 的私有 GPU 動作資料仍按請求在 runtime 背景建立。
- 原作 Dinner 狀態有 S/M/L 曲線；目前第一批設施示範使用 M 曲線。原作完整體型 blend tree 尚待接入，不宣稱已重現所有體型的來源控制器。

## 程式功能

- 選定交談對象且沒有待完成對話時，提供餐廳、休息房間及護理室按鈕；動作準備在既有有界佇列進行。
- 三個自建房間由跨人物共用網格與材質在 runtime 建立；不讀取原作場景或人物卡專用 Cook 資產。
- 活動準備／播放期間保留女生的位置與移動狀態；Esc、超時、結束均回復並銷毀道具，沒有自動好感獎勵。
- 新增稀疏骨骼世界 transform 查詢，供餐具跟隨手部；只取指定骨骼及其祖先，沒有 GPU readback 或全身 CPU pose 求值。
- 這是三種設施的首批流程，尚未實作完整精油施術互動、全部原作道具／IK、所有場所進出、全部 47 種活動或雙人小遊戲。

## 功能證據

- Python：30 passed。包含中文佔位符與假名檢查、音檔／原文／順序不變、完整來源編號、定位點、原作狀態與片段對應、曲線雜湊。
- Runtime-StreetWorldZh01：原版已驗證執行檔搭配新中文，15 次一般語音對話、4 次收集話題交易、3 次觸碰交易通過；823 音檔來源雜湊通過。
- StreetFacilities04：Development 建置 24.95 秒成功。Runtime-StreetFacilities01：passed=true、errors=[]、三種設施的播放／骨骼有效性／退出恢復／關係不變檢查通過。六名可見 10.75 秒，使用既有快取，不能宣稱完整冷啟動時間或性能成績。
- StreetFacilities05：修正截圖必須等待渲染才銷毀設施，Development 建置 32.06 秒成功；Runtime-StreetFacilities02 已通過：passed=true、errors=[]、facilities_verified=3；15 次一般語音、4 次收集話題交易、3 次觸碰交易及 823 音檔來源雜湊通過。三張設施截圖已人工檢查確認有坐姿／躺姿；完整定位、美術及互動驗收未完成。六名可見 11.80 秒，仍為既有快取測試。
- 公共 StreetSocial.cmd 已指向 StreetFacilities05.stage；preflight、執行檔 SHA、四個變更的 root/host C++ 檔案一致性已核對。

## 待完成

1. 檢查設施實際畫面、定位及餐具方向，與來源動作比對。
2. 完整護理互動、體型 blend tree、原作道具定位及 IK。
3. 自建其餘 33 筆場所／設施，場景入口、相鄰關係、活動佔用與生活流程。
4. 尚未涵蓋的舞蹈、運動、購物、體驗與其他雙人活動；閱讀、站立手機、合照、飲料及已完成的共同活動範圍見後續補充與各驗證文件。
5. 持續遵守已實作的共用動作登錄／私有動作惰性建立與有界背景佇列；新增活動不得退回全部私人動作啟動預載。
6. 初始五種性格以外的全人物性格中文與對應聲音覆蓋，以及最後的 GUI／聽音訊／長時間人工驗收。

## 站立手機活動補充
StreetPhoneUse01 新增玩家主動選取的雙人站立手機活動；各自APNG runtime私有動作、左手道具、交談共存及取消恢復已驗證並發布。詳細來源／範圍／證據見 STREET_SOCIAL_PHONE_ACTION_VALIDATION.md。這不把原世界清單action10全部標為完成：自動NPC手機生活排程、坐姿遊戲與完整手機介面仍未實作；其餘世界／施術／體型blend等仍按原清單列pending。

## 自主站立手機補充
StreetNpcPhone02 已補上 NPC 自主手機生活排程、背景私人 APNG 姿勢、手持手機及前景中斷／自然恢復；原先自主手機 pending 已完成。詳細證據見 STREET_SOCIAL_NPC_PHONE_VALIDATION.md。坐姿手機遊戲及完整手機應用介面仍 pending，其餘完整世界清單不變。

## 自主飲料與共用排程補充
StreetAmbientDrink01 將自主手機排程整理為共用生活活動描述，新增站立自主飲料、右手共用自製飲料杯、背景準備、前景中斷及恢復行走。五人飲料／五人手機／既有功能回歸／20組邀請核對已通過；詳見 STREET_SOCIAL_AMBIENT_DRINK_VALIDATION.md。這只補齊站立飲料的自主排程，不把原作完整飲料狀態或其他世界活動全數標為完成。

## 自主自拍補充
StreetAmbientSelfie01 接入 action12 的原作 Shooting 站立循環與 type8 左手手機，共用排程輪替手機／飲料／自拍，任意APNG runtime私有姿勢、五人準備取消／播放中斷／自然結束恢復已通過。詳見 STREET_SOCIAL_AMBIENT_SELFIE_VALIDATION.md；不把完整自拍控制器／相簿或全47行為標記完成。

## 單次無道具整理儀容補充
StreetAmbientGroom01 接入 action15 的 Appearance.Appearance 0／f_tachi_00_14 非循環站立動作。共用活動新增零道具及runtime自然單次完成能力；五人準備取消／播放中斷／自然完成／恢復行走及全部既有回歸通過。詳見 STREET_SOCIAL_AMBIENT_GROOM_VALIDATION.md；完整Appearance狀態機、坐姿／身高混合與其餘生活日程仍待完成。
