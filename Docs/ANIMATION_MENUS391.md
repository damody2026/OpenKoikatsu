# Stage391：動作選單非同步準備與批次清單

2026-10-03。使用通用動作來源／Studio descriptor，不依卡名或動作名特判。

## 修正範圍

所有基本動作、遊戲來源、片段、Studio 動作、手動來源、單人重播共用背景準備入口。首次角色開啟時，也在仍獨占 session 的load worker預熱來源／基本／已選來源與上次片段，才發布session供UI讀取。分類只更新清單；配對姿勢／階段保留既有 Stage383 背景準備，也使用新的批次名稱介面。Outfit／人物資源載入不是本輪動作載入範圍。

每個 Gallery 最多一個正在執行的準備與一個最新待辦。worker 只持有 root、來源、狀態及 Yure 字串，不讀 UObject／角色 session。GameThread 根據世代、角色、LoadGeneration、手動來源文字判斷有效性，過期結果不能改寫新選擇。單人選擇也立即作廢尚在準備的配對世代，已提交的pair則持續播放至單人動作準備完成。準備失敗保留當前播放；瀏覽來源不再先重設 Tpose。準備期間既有動畫持續播放。

新增獨立 FFI 批次 JSON 名稱介面，原 indexed API／CApi layout 不變。ClipSources、ClipNames、ClipNamesForSource 都改批次讀取，避免先 count 再對每個 index 重建清單，亦不再使用固定 512-byte 名稱緩衝。背景先預載 Avatar TOS，磁碟恢復 session 不會到首幀才讀 oo_base。背景 warm 遍歷 blend tree／active layers／base layer 所有可達片段，避免套用後因不同身高才解析新葉片。

Studio H 預載 Yure；普通 Studio 保留原來 Yure 規則。Yure 只快取成功結果，identity 包含 root、state、source、owner 及 controller／scoped／fallback 的 file/zip stamps，IO 失敗的 defaults 與 errors 不快取。既有其它動畫快取的全部失效規則沒有在本輪重寫。

## 與反編譯邏輯對齊

OCIChar.LoadAnime 切換控制器、參數、Yure/PV 與 Play，不重建 DynamicBone 元件。取消 Studio setter 的全配件 reset；真正資源／形狀更換仍保留 reset。Studio setter 已指定 scene=4，不再立即重複 SetGameSceneMode 重算第二幀。

KKS_Colliders.Colliders 的 UpdateFloorColliderBreastDB／UpdateArmCollidersBreastDB／UpdateBreastDB／UpdateSkirtDB 修改既有元件。原 body/clothes 因 flags 改變清空 runtime，重跑 240 預熱，與插件生命週期不符。改為原地配置、保留粒子／計時／私有骨架；Ver02 分開 authored collider 長度及 plugin collider，替換 plugin 尾段，禁止重複累積。plugin 停用時還原 authored radius。首次真正建構物理仍預熱，未降低物理頻率或跳過蒙皮。

參考：C:/portable/KoikatuSunshine/kks_decomp/Managed/CharaStudio_Data/Assembly-CSharp/Studio/OCIChar.cs，及 kks_decomp/plugins/KKS_Plugins/KKS_Colliders/KK_Plugins/Colliders.cs。

## 驗證與限制

GUI runner 用真實 Gallery UMG combo 選擇事件及正式 handlers，真實 March 卡／KKS 來源。測 10 項：Studio 初次／重複、基本來源／還原來源、片段初次／重複、重播、手動來源、快速取代選擇（亦驗待完成pair世代取消）、準備後取消。檢查 request 與 commit <=150ms、正確最新 row／pose、非有限值、畫面幀推進、舊動畫推進、過期拒絕。這不是實體滑鼠自動點擊。ExitCode=0 仍可能 valid=false，必須同時核對 JSON、runs=10 與日誌。

GuiR1 原只驗 request，誤漏 commit 約 2.2秒，不能作最終性能證據；GuiR2/R3/R4 都正確報性能失敗並保留。R4 profiling 顯示 body31.074／clothes77.061／其餘48.278ms，另有骨架取樣成本。GuiR4 在 BuildR7 receipt 尚未存在時啟動，不作最終版本驗收；後續遵守編譯完成→receipt／Succeeded→啟動測試。

GuiR5（BuildR8）全部10項 valid=true：request 1.94–8.88ms，Studio首次commit134.008ms／重複89.096ms，片段20.891／23.101ms，重播19.327ms。初次Studio背景34.526秒、H背景24.321秒，冷解析仍需等待；期間畫面有9561／625幀、H準備舊動畫438樣本，不能稱全部載入瞬間完成或證明GPU FPS提高。首張資源建立／顯式Tpose重設仍有成本，不屬於下拉動作準備的非同步路徑。

Rust ColliderR1：10次設定／碰撞不累積／原始radius恢復；LifetimeR2：普通與Studio setter 配件狀態exact、body/clothes原地配置歷史exact；YureR2：2 tests，包括file/zip修改stamp。正式 BuildR11 actual0／Succeeded。

GuiFinal（BuildR9）首次 commit192.043ms／validfalse，保留；取樣123.110ms，body0.120／clothes0.542／rest58.741ms，不能以GuiR5一次綠燈結案。補Avatar TOS預載後，GuiFinal2（BuildR11）actual0、10cases、validtrue；首次取樣 reset5.030／curves4.573／modifiers1.356ms，完整 frame57.733ms。Studio首次commit65.175／重複60.912ms，H98.410ms；片段18.685／17.314ms、重播19.268ms；來源1.014／0.998ms、手動來源2.190ms。所有 request/commit 均低於150ms。冷Studio背景29.157秒、H23.009秒，後者646幀／舊動畫423樣本。

GuiFinal3（BuildR12）cold commit221.062ms／validfalse；diagnostic mode reset52.068／curves11.713／modifiers2.518／rest141.234ms，未提高原150ms驗收門檻。原來OK_MENU_PROFILE也開啟逐配件OK_ANIM_PROFILE詳細log，現拆成獨立OK_MENU_DETAIL_PROFILE；正式runner清除詳細日誌環境，正常模式重新測量。尖峰和失敗log全保留。

Studio Yure R1／Source R1並行Unreal automation互相discover worker並收到對方controller訊息，actual1、無TestCompleted；HTTP ports碰撞。此類UE測試共用messaging／Saved/Automation，不再並行。YureR2逐項執行actual0、5cards/450frames，每卡retained_normal／fallback／invalid_atomic／cold_eye_look通過；冷建Alice／Yuuka等卡耗時，完整9分鐘回歸未因耗時被視為成功。

GuiFinal4（正式BuildR14 actual0／Succeeded）正常模式actual0、10cases／validtrue，保持request/commit<=150ms門檻。Studio首次19.809ms／重複20.226ms，generic片段17.307／17.211ms、replay14.762ms；來源1.012／0.789ms、manual2.103ms、H52.797ms。cold Studio背景35.729秒、H22.799秒；H準備期间636world frames／422舊動畫samples。跨單人/pending pair generation與late cancel都通過。詳細profiling不是最終一般模式計時；不能把這些数字當GPU FPS。

初始UI animation_catalog142.728ms，仍有首次建Unreal模型11.423秒／face-hand2.730秒等GT成本，沒有在本輪宣稱首張角色載入全無停頓。移到角色worker的是動作目錄解析／已選片段準備，角色模型建立另有成本。

最終BuildR14之後逐項Unreal回歸全部actual0／AutomationSuccess：Anim391YureR3（5cards/450frames），Anim391StudioR2（5cards、normal/H/layers、135frames與45無效參數atomic），Anim391ControlsR1（7mask Unity oracle、真NoCapture/Slate boundary、UI/focus/clock），Anim391NativeR1（March/KK_224305、原walk、shared pair clock、4筆active IK old-solver exact、667/677 palette、skin/world/geometry）。Rust與GUI的完整成功／失敗證據皆保留。

## 保存

所有中間失敗、receipt、profile、fixture、source.diff 都保留在 Saved/Tests/AnimationMenus391。其它同時存在的 uncensor/project-resource、README、動作繁中名稱變更保留；本輪不把那些修改列成自己的成果。source.diff 是相對 Stage390 snapshot，可能包含同檔並行變更，需依本文件範圍辨識。

最終封存：Saved/Tests/AnimationMenus391/R1.audit.json 與 R1.snapshot。核對Stage390 R1的305項旧證據；seal自身stdout直接回傳，不放到正在封存的目錄。

補同fixture NativeR2 actual0／AutomationSuccess：前輪DefaultData/chara/male/ill_male_02與March，4activeIK exact、667/681 export及skin/world均valid；NativeR1另測KK_224305為667/677，兩張男卡回歸皆通過。封存首個assert於寫audit之前發現fixture bonecount混用，已更正，未變更產品骨架或降低parity條件。
