# Stage380：非同步卡片載入的一致性與跨卡快取驗證

GUI 的 OnLoad3D 以背景 thread 載入捕捉的 root/card/outfit；完成後 FinishLoad3D 會使用 GUI 目前所選卡片的身形、語音與 PreviewCardPath/PreviewOutfitIndex。若其間容許切換選取，會產生模型與 metadata 不一致。這是所有卡片共用的時序問題。

修正載入期間的選取保護：SetLoadBusy 禁用卡片清單、outfit navigation 與 combo；SelectIndex、ScanAndShow、三個 outfit callbacks 都檢查 busy。阻擋 combo callback 時重新同步顯示，完成後恢復操作。原有同卡身形編輯仍可修改 Selected，完成後僅在形狀值不同時呼叫原本 ApplyShapes。沒有改動 native shape、材質、shader 或卡片內容，也沒有依檔名修補。

驗證分兩部分：OpenKoikatsu.Gallery.LoadSelection 直接呼叫實際 widget callbacks，核對忙碌時 identity 不變、解除後恢復操作與身形仍可編輯。OpenKoikatsu.Preview.WarmLoad 的 switching 模式以實際 card parser、native session、PreviewActor 依序載入 March_7th outfit0、ill_male_02 outfit0、ill_male_02 outfit1，再依序重新載入；逐項核對每個上傳 mip 的 hash、尺寸、SRGB/filter/wrap。第一張另執行身高修改及還原，確認 native session 值與上傳贴圖資料。

使用 Tools/run_card_warm380.ps1，必須 fresh Tag；OtherCard 參數啟用六次切換模式，完整測試需 actual exit0、Automation Success、run6 與 texture parity valid1，不把只成功啟動當通過。所有測試保留在 Saved/Tests/CardWarm380 與 Saved/Logs/CardWarm380*。

BuildR1 被重新開啟的主 Editor Live Coding 阻擋，因此使用獨立 BuildHostR1/WarmAudit.uproject，複製同份專案/插件 C++ source 與 Rust lib，不改主 Editor 已載入的 DLL。BuildHostR1 曾因測試 include path 與 engine NetCore DLL 正在使用而失敗；修正 include 與獨立 UBT -UsePrecompiled 後 BuildHostR2 actual0。後者只使用現有引擎 binary，不把停止使用者 Editor 或重連結 engine 當成前提。

最終驗證：SelectionHostR1、SwitchHostR1 均 actual0／Automation Success。六次 native session/actor 建構成功，每一組的全部上傳 mip hash、尺寸與 SRGB/filter/wrap 在 replay 相同；引用數依序134、60、56。March_7th 修改身高／還原原值，native session shape 值正確且貼圖不變。

| 來源 | 首次 session＋建構＋catalog | replay | replay textures |
| --- | ---: | ---: | ---: |
| March_7th outfit0 | 24.340 秒 | 7.911 秒 | 134 |
| ill_male_02 outfit0 | 128.974 秒 | 2.182 秒 | 60 |
| ill_male_02 outfit1 | 83.808 秒 | 2.021 秒 | 56 |

後半三次 miss 均保持137，hits 從110增加到357，137entries 共108616491bytes，沒有 PNG 重解碼或 mip 重算。男角首次 open 包含127.106／82.681秒 native 資源建構；不能把它當 warm cache 回歸。March replay 本輪主 Editor 同時運行，因此不能將7.911秒與 Stage379 的獨立4.816秒直接視為性能退化；檢查目標是切換後資料一致與無新增 decode，並未修改 Stage379 快取演算法。

封存 Saved/Tests/CardWarm380/R1.audit.json 與 snapshot，原 Stage379 的266項證據逐檔保留。source 與獨立 host 編譯來源逐檔 SHA 相同。R1 封存時主 UnrealEditor PID67896 仍開啟，因此 R1 記錄正式 DLL 未更新；此為當時狀態，後續正式編譯與驗證如下。

回歸不等於所有卡片/GPU pixels/Shipping 無 bug，數據也不含完整 GUI 語音與額外控制工作。


2026-10-03 正式驗收：使用者關閉主 Editor 後，確認沒有 UnrealEditor/UnrealEditor-Cmd 在運行，正常 Build OpenKoikatsuEditor Win64 Development（-WaitMutex -NoUBA，未繞過 Live Coding）BuildProductionR1 actual0／Succeeded。正式主 DLL 已更新；源碼與獨立 host 逐檔 SHA 核對沒有差異。

SelectionProductionR1、SwitchProductionR1 使用主 OpenKoikatsu.uproject 與正式 DLL，actual0／Automation Success。六次 session/actor 建構、忙碌時卡片與 outfit 保護、解除後恢復、同卡身形修改／還原、134／60／56 貼圖引用的每個 mip／尺寸／sampling／SRGB parity 全數通過。三組 replay 耗時分別 5.577／2.032／1.927 秒；miss 保持137，hits110→357，bytes108616491，沒有重解碼或重算 mip。

正式證據封存 Saved/Tests/CardWarm380/R2.audit.json 與 R2.snapshot；R1 的220項與 Stage379 的266項舊證據保持原樣。正式編譯與測試待辦已完成，可重新開啟主 Unreal 使用修正。此測試範圍與上方限制維持不變。
