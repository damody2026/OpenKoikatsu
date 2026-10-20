# Saved 清理紀錄（2026-10-04）

使用者授權清除未使用檔案，目標小於200GB。完整盤點（不跟隨junction）921.530GB/858.242GiB；NativeImportSources為787.276GB，大量是連到Content資源與原zipmod的hard links，顯示容量不等於可釋放磁碟空間。

保留Tools/card_animation_cases373.json/374.json指定兩份來源視圖、全部APNG/角色資產、DevelopmentHost的source/binaries/intermediate/cooked、D槽正式stage、完整utrace與小型統計/報告。僅移除20份未使用可重建private import views和54份可由保留trace及commands重產的原始CPU/GPU event CSV。全部目標先驗證在Saved內、無reparse後代；不刪Content junction，不停止其他程式。

第一次unique-data防護檢查發現未列入白名單的空白.ok_override_index.tsv/.ok_guid_index.txt並拒絕，尚未刪除。核對它們是可重建索引後加入cache白名單；再次檢查所有真正來源資料仍有Saved/NativeImportSources以外的hard link，通過。原始資源檔案不會因unlink舊視圖被刪除。

預計移除顯示733.515GB，剩188.015GB；實際完成與磁碟釋放量將另行核對。操作清單在同名_plan.json，逐項結果在_deleted.jsonl。

## 完成與驗證

- 74個目標全部刪除成功（20目錄、54CSV），無刪除錯誤。
- 重新完整盤點：188,015,424,343 bytes = **188.015GB / 175.103GiB**，已低於200GB；289,424檔案。
- 顯示/邏輯容量減少733.515GB。C槽可用空間從187,735,965,696到198,342,909,952 bytes，觀察增加約10.607GB；其它程式仍可能寫入，所以不是排他性精確磁碟reclaim證明。hard-link共享原始內容仍保留，不能宣稱真的釋放733GB。
- 100張選卡的200份Character/Walk package存在，1146份原生資源catalog檔案長度正常，APNGCards與IndependentCards總bytes完全不變，低模驗收/bounds/Cooked map仍在。
- D槽正式Development exe SHA256仍為3EF359D7178C35A7CEA53888CA3F3B03D7E68ED1AD4B6CC096160EDFDF75DE73。
- 未啟動效能測試；使用者已說目前有其他程式在跑。未刪除Content、Config、原始APNG/女角、高低模、正式遊戲stage或完整utrace；原始event CSV可由保留的commands/trace重新匯出。
