# 共用一對一模式驗收

## 實作

兩個入口共用 AOKInteractionMode、UOKCharacterController、UOKInteractionWidget 與既有配對播放控制。模式在原程序與原 World 建立獨立男女高模 session、舞台及相機；退出後釋放模式資源並還原來源。StreetSocial 不再建立另一個 Editor 程序，也不再以返回 JSON 輪詢控制遊戲。

讀卡來源 session 以包含材質的 portable snapshot 複製，保留未保存編輯；男女來源各自判斷原始 KKS 或 APNG 格式。來源角色、可見性、tick、碰撞、相機、輸入與語音暫停狀態由模式保存。StreetSocial 另保留來源街區活動、時間與關係，不建立第二套存檔。

共用控制提供動作分類／搜尋、姿勢及階段、暫停、重播、速度與男女共用時鐘。動作背景準備完成後才切換；載入可取消，錯誤返回來源。沿用既有相機、語音、表情、嘴型及身體互動控制。

## 已完成證據

- 指定 KK_384914.png：現有根專案 R16、隔離重構 R18、正式根專案 R19 四視角皆通過，均實際檢視。高光、輪廓、膚色光影保留；詳見 SHARED_INTERACTION_RENDER_PRESERVATION_20261009.md。本重構沒有調整 render。
- 正式讀卡 `Saved/Interaction/ReaderRootShared40KK384914/validation.json`：8 項通過，三次高模往返保留未保存編輯及相機；暫停／重播／速度／男女時鐘、不同姿勢與階段、載入取消及缺失資源皆驗證。
- 正式 packaged 建置 SharedInteraction39 的 Presentation：15 組陌生／朋友／戀人控制及數值同步通過，站立、跑步及 UI 呈現通過；既有 FXAA 150% 保留。
- 同建置 Dialogue：原作來源對話上下文 20 組，成功與拒絕告白各 5 位女角通過。
- 同建置 Touch：20 組身體點觸、位移、根位置保留及重複觸發限制通過。
- 同建置 C019、C044、C004：各 9 視角 GPU／來源幾何比對通過，無 cooked shader fallback。此為本系統 CPU／GPU 一致性證據，不宣稱與原作逐像素一致。
- Python StreetSocial 測試：73 項通過。

## 最終街區回歸與入口

`Saved/StreetSocial/Runtime-SharedInteraction39Full/social.validation.json` 完整回歸通過，無 errors。五位女角 × 三種關係共 15 組高模互動，均與街區使用同 PID，雙人時鐘持續前進，返回後來源街區恢復。共同活動、話題、語音、手機、設施與自動關係保存亦通過現有完整驗收。

嚴格發布工具重新計算話題、來源對話版面、手機、15 組聯絡／高模、觸碰與外觀證據後，才更新 `Saved/StreetSocial/Build/receipt.json`。正式執行檔 SHA256：`a8aef5290afbd94b62140d063aa9055327aa72683ab3d2757477208400dbd08c`。receipt 的 runtime_interaction_mode 為 in_process，沒有舊互動 host 欄位；前版 receipt 有備份，保留的其他歷史證據明確註記原建置來源。

StreetSocial.cmd 啟動器 DryRun 通過並使用 SharedInteraction39.stage；CardReaderDevelopment.cmd 啟動器 DryRun 通過並使用根專案 20261009 後綴模組。讀卡原始 DLL 保留、原開啟程序未中斷。重新啟動兩個入口即可使用新模式。

## 範圍

本次確認當前功能與指定卡渲染保留，不宣稱原作逐像素一致、所有卡片任意動作都相容，亦未重新進行效能目標驗收。三次讀卡及 15 次街區往返驗證了生命週期；不把有限回合測試當成無限時間壓力測試。

## 發布後再次核對

2026-10-09 18:06：重新核對計畫與當前入口，沒有未完成項。兩個啟動器 DryRun 再次通過；Street 正式執行檔及讀卡 8 個模組 SHA256 與驗收版本一致，reader 證據雜湊一致。Full、Presentation、Dialogue、Touch 報告皆 passed 且 errors 空，讀卡 8 項皆通過。未發現新的功能錯誤，未修改 render 或為已通過功能重建程式。
