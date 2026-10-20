# 角色編輯器實作與驗收（2026-10-08）

## 計畫與範圍

1. 盤點原版 44 個身體、52 個臉部形狀參數、胸部物理參數、ABMX、Pregnancy+、動畫對應與衣服狀態。
2. 補齊通用資料保留、左右側與服裝座標處理，避免依賴測試角色或特定機器路徑。
3. 以正式根專案建置，執行單元、原生模型與真實編輯器驗收；逐項播放所有可用 Demo。
4. 保留失敗與成功證據，完成後更新本文件，不把抽查當成完整驗收。

本次範圍是已加入的形狀／骨骼／腹部數值控制與動畫、換衣、重置、儲存流程。圖片中的材質編輯器、眉毛／眼線圖案資產選擇，不納入數值參數完成宣稱。

## 驗收清單

- [x] 原版單項／全項性別預設重置：四組原始預設逐值比較一致，ShapeReset PASS。
- [x] ABMX 定義、左右側及序列化；服裝座標隔離：EditorMods / EditorModsCoordinates PASS；1,192 控制與源碼一致。
- [x] 未編輯插件、同插件未知欄位與其他服裝設定保留：原生 ExtendedSave round trip PASS，未知未來版本拒絕覆寫。
- [x] Pregnancy+ 16 項控制在真實模型上逐項產生形變、有限數值、往返無累積；原生停用／還原 PASS；11 預設與插件源碼一致。
- [x] 所有 739 個可用 Demo 從編輯器選擇直接播放，沒有檔案選擇步驟。
- [x] 半脫／全脫／穿回與單件切換；保持動畫與參數。
- [x] C000 / C001 / C018 各 5 項真實編輯流程 PASS；編輯 PNG 另存不同資料夾後原生模型重新開啟 PASS。
- [x] 最新正式建置、五項編輯器回歸、原生雙角色回歸與三個角色畫面檢查。

## 結果

最終 FullDemoR8：PASS，739 / 739 個 Demo 逐項由編輯器選擇及實際播放，7 / 7 衣服流程通過，共 746 筆。最新三角色各 5 項流程、五項單元及衣服快速序列也全部 PASS；程序 exit 0，執行前後兩個正式 DLL SHA256 一致。沒有未完成的驗收項目；缺失原始資產的範圍限制列於下方。

先前 FullDemoR5 揭露的單件連身衣連動與 R7 驗收對正常 no-op 的誤判，均已修正並重新從第一個 Demo 全項播放。失敗輪次及原因保留在錯誤文件，不改寫為 PASS。

| 項目 | 結果與證據（相對專案根目錄） |
| --- | --- |
| 正式建置 | PASS，`Saved/Tests/EditorAcceptanceBuildR8.log`，官方正常建置完成後才啟動驗收；本次編輯器變更在 R7 → R8 僅修正測試 fixture／no-op 斷言，另重跑三角色以驗證共用插件的最新連結結果 |
| 完整 Demo + 衣服 | 746 / 746 PASS，739 Demo + 7 衣服；`Saved/Tests/EditorAcceptance/FullDemoR8/report.json`、`exit.txt`、`launch.json` |
| 編輯器單元回歸 | 5 / 5 PASS：EditorClothing、EditorMods、EditorModsCoordinates、ShapeReset、AnimationLookup；`Saved/Tests/EditorAcceptanceUnitR8.log` |
| 衣服完整序列 | PASS，`Saved/Tests/EditorAcceptance/ClothesR8/report.json`，9 Demo + 7 衣服；單件全脫 top 三角形 0、下裝狀態 3，全脫衣服總三角形 0，穿回 25,934；重複穿上合法重用 |
| 原版預設 | 男女各 44 身體 + 52 臉部，192 個值全相等；`Saved/Tests/EditorDefaultsReferenceR2.log` |
| Mod 來源比對 | 1,192 ABMX 控制、11 Pregnancy+ 預設相等；`Saved/Tests/EditorControlsReference.log` |
| 原生資料保留 | 3 / 3 PASS，包含未知欄位與陣列、拒絕未知版本；`Saved/Tests/EditorModsNativeAcceptance.log` |
| 腹部形變 | 2 / 2 PASS，逐項覆蓋 16 控制、停用還原、無累積；`Saved/Tests/EditorPregnancyAcceptance.log` |
| 共用動畫目錄 | 1 / 1 PASS，同包不同 owner 共用原始目錄且控制器 graph 不混用；`Saved/Tests/EditorCatalogueAcceptance.log` |
| 三角色編輯 | 最新 C000 / C001 / C018 各 5 / 5 PASS；`Saved/Tests/EditorAcceptance/ModsC000R8/report.json`、`ModsC001R8/report.json`、`ModsC018R8/report.json` |
| 原生服裝規則 | 2 / 2 PASS，連身／獨立衣服、合法狀態、全脫／穿回、資源 digest 驗證；另存至新資料夾後移除原始來源紀錄，規則仍可離線讀取；`Saved/Tests/EditorClothingRulesR8.log` |
| 儲存再讀 | EditorMods 另存不同資料夾後，以空遊戲根路徑重新開啟 portable PNG 並確認 1.25 骨骼倍率；包含於最新五項回歸 |
| 雙角色原生播放 | PASS，兩個原生配對選擇、獨立／共享時鐘、IK 與背景準備時舊動作持續前進；`Saved/Tests/EditorPairedRegressionR5.log` |
| 畫面 | 已檢查 `ModsC001R8/report_abmx.png`、`ModsC018R5/report.png`、`ModsC000R7/report.png`，長標籤不重疊且重置不換行、腹部形變可見；三角色最新 R8 亦各保存兩張畫面 |

## 通用處理與限制

- 修改 ABMX 時只更新 boneData，保留同插件未知欄位及其他插件；不支援的版本拒絕覆寫並回復 UI。角色卡 schema 與服裝卡 schema 分開判定。
- 支援左／右／兩側與各服裝座標；只有一側的自訂骨架仍可選擇存在的側。缺失骨骼停用該控制，不套用到其他骨骼。
- 單角色編輯器不提前解碼全部雙人配對；有配對角色時恢復原本準備入口。同包多控制器只共用原始 clip 目錄，動作來源 key 保持獨立。
- 半脫／全脫透過原生 portable 服裝狀態資源切換，保留當前動作和 mod；Pregnancy+ 更新 mesh 來源而保留 GPU 播放元件。
- 單件切換共用原生 SetClothesState 的 StateType、notBot、notShorts 和鞋子連動；規則來自原版／mod 清單、子部件與 DEF 參考，不以角色名指定。按資源 digest 與服裝座標儲存及另存複製；第一次準備需要原始轉換來源，準備後可離線使用。
- 當前資源有 739 個可解析的 Demo。原始表中的 4 個狀態不存在於對應控制器，另有 `vstudio/anime/00.unity3d` 缺失；`editor-demos.json` 的 unavailable_states / unavailable_bundles 記錄原因，不以不同動作假冒。此處不宣稱已補造缺失的原始資產。
- Pregnancy+ 驗收是數值控制與實際幾何形變，不宣稱完整原插件材質功能或逐像素渲染相同。
- 可重跑入口：`Tools/run_editor_acceptance.ps1 -Tag <新標籤> -Card <PNG> -Mode Demo|Clothes|Mods`。Demo 是全清單，Clothes 是明確標示的 9 Demo + 7 衣服快速流程。每輪新資料夾、檢查 BuildId 與 DLL SHA256、拒絕不完整結果並保留失敗證據。
- 單狀態準備入口：`Tools/ok-extract/target/release/ok-preview-pack.exe prepare-clothing-state <PNG> <服裝座標> <九項逗號分隔狀態>`。只準備指定服裝座標，沿用原本 verified receipt／資源 hash 驗證，原始 PNG 不改寫；`Saved/Tests/EditorClothingTopPreparationR8.log` 為上衣連身全脫實際資源證據。

## 最終版本

- OpenKoikatsu DLL SHA256：`4B416EF54C4E68AB22327841EC67E0C3727935AA3684ECDDAE3D3FE37EA6E4B9`
- KKUnityBundle DLL SHA256：`9CAE763F8BD30E910F6B7AD23BD023E943352A5959D40A98458678977D54491C`
- 完整結果摘要：`Saved/Tests/EditorAcceptance/final-summary-r8.json`。
- 已啟動正常可操作編輯器，PID 40008、Responding=true、C000 3D 載入 `ok=1`；`Saved/Tests/EditorAcceptance/preview-final-r8.log`。啟動時短暫忙碌已結束。

## 續作核對（2026-10-08 04:26，Asia/Taipei）

重新核對目前兩個 DLL SHA256，仍與上述 R8 完整驗收相同。主要編輯器／原生模組來源的修改時間均早於正式 DLL 建置；本輪沒有新增功能程式變更。

FullDemoR8 746 筆、ClothesR8 16 筆、三角色各 5 筆報告皆 valid=true／exit 0；UnitR8 日誌五項 Success，預覽 PID 40008 正常回應。當前數值編輯、重置、動畫與服裝功能沒有未完成項目，原始資產與材質／圖案選擇的範圍限制仍依前文。僅更正表格殘留的「四項回歸」為實際「五項」，未將此次證據核對宣稱為新的一輪播放測試。

## 2026-10-08 左欄顯示設定與拖曳即時更新（R10）

- 左欄頂部新增「編輯欄寬度」（320–960）與「顯示字級」（12–24），可以拖曳數值或直接鍵入。字級同步縮放文字、按鈕及滑桿點擊區域，較大字級會提高最小欄寬以保持可操作。使用者設定保存於 GameUserSettings.ini 的 OpenKoikatsu.EditorDisplay，不寫入角色卡。輸入停下後保存；Widget 關閉時也保存尚未寫入的值。
- 移除 body/mod 的 0.15 秒停手 debounce：NativeTick 每畫面套用最新待更新數值，ABMX、身體物理、Pregnancy+ 共用排程，連續輸入不再延後到放開。現有模型與動畫持續使用原 Session。
- 建置：Saved/Tests/EditorDisplayLiveBuildR10.log，Result: Succeeded。首次 R9 的測試型別錯誤與修正保留於錯誤紀錄。
- 實際編輯器 LiveC000R10 五項 PASS / exit 0：欄寬與字級及設定磁碟讀回、縮小版面、連續骨骼輸入期間更新、連續 Pregnancy+ 輸入期間 GPU mesh 更新、最後數值／全部重置自動套用。測試每畫面觸發輸入，不手動 ApplyPendingEditorMods，也不送放開事件。600 寬／24 字級與 320 寬／12 字級截圖已逐張檢視，捲動區保持可用。
- 實際編輯器 ModsC000R10 五項 PASS / exit 0：單側骨骼、Pregnancy+ GPU 顯示、Demo 播放、半脫保持參數與動畫、全部重置。
- 自動化回歸五項 PASS：EditorDisplayLiveUnitR10.log 四項（EditorClothing、EditorMods、EditorModsCoordinates、ShapeReset），EditorDisplayLiveAnimationUnitR10.log 一項（Motion.AnimationLookup）。第一次命名空間誤植有記錄，不能將第一輪四項寫成五項。
- Live / Mods 驗收前後 DLL SHA256 相同。OpenKoikatsu：036A03932C97E0B9509D8FE414393BA10B869E26B45A2088365C9DDCC36311E4；KKUnityBundle：9CAE763F8BD30E910F6B7AD23BD023E943352A5959D40A98458678977D54491C。
- 本輪沒有修改 Demo 對應表或服裝規則；739 動作完整結果保留於 FullDemoR8，本輪僅列當前實際執行的回歸，不聲稱重新跑過 739 動作。

## 2026-10-08 編輯器設定繁體中文（R12）

- 1192 個骨骼控制項翻譯為繁體中文，包含頭臉、胸部／乳頭、軀幹、手腳、手指、裙子與私密部位的縮放、長度、位移及旋轉。產生器使用 Tools/editor_bone_terms_zh_tw.json，未知英文術語會拒絕產生，避免更新後漏翻。
- 11 個腹部形變預設新增中文 Label；來源 Name／JSON、1192 項骨骼識別碼、分類鍵、軸向與數值範圍保持來源值。verify_editor_controls.py 已比對安裝的 ABMX 與 Pregnancy+ 原始碼，全部通過；也檢查中文覆蓋與名稱唯一性。
- 中文分類「骨骼調整」「腹部形變」及所有對應 UI callback 同步更新。補齊Ｔ字站姿、眼睛跟隨鏡頭、頭部看向鏡頭、工作室分類、動作選項、手勢甲乙混合、油漆單位／色彩名稱及原遊戲參數的繁體字。
- 動態動畫數值使用獨立中文顯示：身高／胸部校正、速度、動作混合、動畫層權重／開始時間／起始進度等；任意自訂控制器保留原始參數鍵與操作，顯示為具有唯一來源順序的「自訂動畫參數 N」。不改寫播放控制器或卡片資料。
- 成功建置：Saved/Tests/EditorChineseBuildR12Retry.log。最終 DLL：OpenKoikatsu SHA256 45A33A97819F50489CBB66C2DB314B5C052D2E192105702F1280E7046E8447F3；KKUnityBundle SHA256 9CAE763F8BD30E910F6B7AD23BD023E943352A5959D40A98458678977D54491C。
- LiveChineseR12：6 項 PASS / exit 0，中文骨骼與預設選單、欄寬／字級／設定保存、緊湊版面、持續骨骼輸入、持續腹部形變 GPU 更新及最後重置。ModsChineseR12：5 項 PASS / exit 0，中文預設實際套用、骨骼、GPU 顯示、動畫、半脫與重置。
- EditorChineseUnitR12.log：ChineseSettings、EditorClothing、EditorMods、EditorModsCoordinates、ShapeReset、Motion.AnimationLookup 六項全部 Success。最終共 11 項實際編輯器檢查與 6 項自動化回歸；兩份實際驗收前後 DLL 雜湊保持一致。
- 已逐張檢視 LiveChineseR12/report_small.png、LiveChineseR11/report_large.png（同一套中文靜態標籤）與 ModsChineseR12/report.png；中文控制項與「標準」預設正常顯示。本輪不重新執行未修改的 739 動作完整覆蓋。


## 2026-10-08 圖片式外觀選擇（最終 R14）

- 左欄「圖片式外觀選擇」開啟右側圖片網格，提供中文部位選單、搜尋、32 項分頁、隨機、類型重置與選中標記。涵蓋 53 個原遊戲素材分類、300 個卡片欄位，包括頭型、眉毛、上下眼線、眼白／瞳孔／高光、妝容、臉部與身體材質／彩繪、頭髮／光澤、服裝／花紋／徽章及 20 個原始配件欄位。
- 依安裝遊戲與 zipmod 的素材清單建立目錄；最終為 38,723 個可檢視項目（原始遊戲 1,708、模組 37,015）。同編號不同 GUID 分別保留；12 組重複安裝的 GUID 依與原生載入器相同的有效來源顯示。六個缺少 GUID 的來源無法安全寫入原遊戲 resolver，套用時明確拒絕並保留目前角色，不猜來源。這是通用資料目錄，沒有硬編碼示例眉毛或模組白名單。
- 真實縮圖支援 Sprite 圖集裁切／旋轉、Texture2D 與模組 PNG；原始 53/53 分類、眉毛第一頁 32/32 與額外模組上眼線 32/32 讀取成功。模組本身沒有縮圖檔案時顯示「素材未提供縮圖」，不以假圖掩蓋。實際測試 DEF Mangekyou 的 zip 只有縮圖目錄、沒有縮圖檔案，其缺圖提示符合來源。
- 點選後背景保存目前參數、修改真正卡片欄位及 UAR，重建獨立角色資源並更新 3D。原卡保留，工作卡可另存；未知區塊、全部服裝、其他模組與數值保留。舊 lean 卡以外觀、全部服裝與不可變角色身份共同精確還原原始 KKEx，有歧義即拒絕，不猜檔名。新卡持有完整原始資料與服裝準備 receipt。
- 材質編輯器只清除所選部位會覆蓋新類型的衝突資料，保留其他部位；上下眼線、瞳孔高光與衣服 MainTex 多圖層具有各自範圍。圖片套用前後檢查實際貼圖 CRC 改變及未修改身體頂點數一致，不能只以編號／GUID 改變判定。
- 成功建置：Saved/Tests/AppearanceBuildR14.log。最終 DLL SHA256：OpenKoikatsu `B39F0371308F694A82C942E168B6AA70F0DDC0D827C9D58C4B9857D3C851C26E`；KKUnityBundle `9EB6F049C75CFC9CCF3315C88B92B7FD721096926EDC5373E31E1F49A208404E`。
- 同版實際編輯器：AppearanceR14 八項、AppearanceLiveR14 六項、AppearanceModsR14 五項，全數 valid=true／exit 0，前後 DLL 雜湊相同。共 19 項，包含原始／模組縮圖、搜尋分頁、點選後真正貼圖與磁碟回讀、模組 GUID／貼圖、重置、半脫／全脫／穿回、欄寬字級／中文／連續輸入 GPU 更新，以及新外觀卡的骨骼、腹部形變、動畫與半脫。
- 自動化：AppearanceUnitR14 六項全部 Success（ChineseSettings、EditorClothing、EditorMods、EditorModsCoordinates、ShapeReset、Motion.AnimationLookup）。AppearanceNativeR14Retry 九項全部 PASS，包含 300 真卡欄位、保留其他值、舊來源恢復、材質範圍及重複 GUID 有效來源。
- 補充實際資料驗證：AppearanceLegacyR11 的舊 C000 更換眉毛後保留七套服裝，Coordinate／Parameter 位元組相同，七套身體均 9558 頂點；七套獨立載入與身形幾何測試皆通過。AppearanceMovedR11 搬動後的卡在原暫存來源不可用時仍能從卡內準備半脫，服裝可見三角形由 25,934 變為 19,428。上述來源／服裝路徑在最終版本未再修改，保留其原生建置輪次，不冒稱為 R14 重跑。
- 1192 骨骼控制與 11 腹部預設另以原插件 InterfaceData.cs／BellyTemplate.cs 精確比對，含繁體中文覆蓋，AppearanceRegistryR12.log 通過；最終 R14 的中文與連續輸入行為也已重新實測。
- 已檢視最終眉毛、上眼線與模組套用畫面；截圖等待著色器完成與至少兩秒穩定時間，排除準備中的黑畫面。沒有宣稱逐一渲染全部 38,723 項；739 動作的完整播放證據仍為先前 FullDemoR8，本輪重新驗證對應表與新外觀卡的動畫整合。
- 完整結果：Saved/Tests/EditorAcceptance/appearance-final-r14.json。重跑入口：Tools/run_editor_acceptance.ps1 -Tag 新標籤 -Card PNG -Mode Appearance；修正與失敗詳見 Docs/CHARACTER_EDITOR_ERRORS.md 的 E29–E43。


## 續作核對（2026-10-08T10:16:26+08:00）

本輪重新核對兩個正式 DLL SHA256，仍與 appearance-final-r14.json 完全一致；圖片選擇、Gallery 編輯介面與 Editor 驗收來源及全部 Rust 來源均未晚於其正式 DLL。三份實際編輯器報告仍 valid=true／exit 0，共 19 項；六項自動化回歸與九項原生測試的成功紀錄保持完整。沒有發現本次功能未完成項目，亦未修改功能程式或重跑相同版本的完整測試；這次是版本與既有驗收證據核對，不另宣稱新一輪完整播放。錯誤紀錄維持 E29–E43，本輪核對未遇到新錯誤。

## 左右手獨立動作最終驗收（2026-10-08 10:33 +08:00）

- 左欄新增「左右手動作」入口，直達右側動作欄頂部。左右手各自選甲／乙手勢與混合比例，各有「跟隨動作」重設；跟隨時停用乙與混合。數量讀自原作資源，目前每側 24 種，沿用 ShapeHandInfo 的左右各 15 個手指目標。
- 修正 portable 靜止模式的手勢刷新：使用 StaticInteractionFrame 從 shaped baseline 重算，無需原遊戲 game_root；播放模式沿用原生／Unreal Animator 的現有更新流程。單次與批次更新一致，不額外推進動畫或物理時間。修正窄動作欄並排重設文字重疊，改為完整欄寬、可換行按鈕。
- HandsR3 實際 UI 驗收八項全部 valid=true、exit 0：左右手全部 24 項各自生效且對側不變、獨立混合、左側重設還原基準且右側保留、兩側覆寫在 demo 播放與切換後保留、半脫重載後保留、右側重設時左側不變。最終截圖已檢視，左右手入口、獨立選單與兩個重設按鈕可見且無文字重疊。
- HandsUnitR3 三項自動化全數 Success：AnimationCatalog.HandShapes、Gallery.ChineseSettings、Gallery.ShapeReset。原生 HandsNativeR1 三項通過，原作 58 組操作、1,972 筆骨骼資料比對最大誤差 0.000000119；此次沒有修改 Rust 手勢來源與演算法，沿用該比對證據。
- 成功建置：Saved/Tests/HandsBuildR3.log。本輪最終 DLL SHA256：OpenKoikatsu `74B8B5466E13DFEFD5F91C6499968A6EABC94CCF6C1AD04D0CD682513759F552`；KKUnityBundle `9E661CDEA14A7F2619D4085601683E0F936B2FF2A34FE21BED56E15529E17987`。HandsR3 啟動、結束與最終核對雜湊一致；上述為新版本證據，前段 R14 為當時圖片選擇功能的版本紀錄。
- 結果：Saved/Tests/EditorAcceptance/hands-final-r3.json；重跑：Tools/run_editor_acceptance.ps1 -Tag 新標籤 -Card Content/CharacterCards/CrowdAPNG/C000.png -Mode Hands。保留 HandsR1 的真實失敗報告與 HandsR2 的功能通過、版面修正前截圖，錯誤及預防方式記在 E44–E46。

## 左右手功能續作核對（2026-10-08T10:34:13.6280136+08:00）

本輪核對兩個正式 DLL SHA256，均與 hands-final-r3.json 一致；左右手 UI、事件、實際驗收與角色更新程式均未晚於對應 DLL，沒有尚未建置的功能修改。HandsR3 八項實際編輯器驗收全數 valid=true／exit 0；HandsUnitR3 三項自動化與 HandsNativeR1 三項原生測試成功紀錄完整，包含原作 1,972 筆骨骼比對。本輪沒有發現未完成項目或新增錯誤；保留 E44–E46 的原因與修正，沒有重跑相同版本測試或另稱新增驗收輪次。

## 服裝快速切換最終驗收（2026-10-08 11:02 +08:00）

- 實作背景準備穿著、半脫、全脫，以及全脫後再半脫的常見狀態。最後一種保留其餘五個服裝槽位的全脫狀態，沿用原作連動與可用狀態規則。記憶體快取最多保留三個非作用中預覽與一個背景準備工作；來源、服裝座標、資源時間戳、九槽狀態及獨立／原遊戲模式納入身份，失效時釋放舊預覽，過期工作不發布結果。
- 快速切換重用原生 Session、模型、材質與 GPU 資料，不重新建立整套角色／選單／語音。新編輯參數透過即時更新路徑同步；保留身體／臉部、骨骼參數、左右手勢、選定動畫、浮點參數、動畫時間、速度、暫停狀態與鏡頭。語音目錄及音高複製至作用中預覽，閒置預覽停止更新及播放。沒有準備好的個別衣物組合仍走既有可靠載入路徑，完成後也進入有界 LRU。
- 原生背景載入新增明確的服裝狀態 API，使用可巢狀、可在 unwind 還原的 thread-local scope，SQL Session 快取鍵亦使用同一明確請求，不修改 process-global OK_CLOTHES_STATES。材質非阻塞準備完成才視為快速可用；已完成的空編譯資源清單不等待無關全域 shader 工作。
- ClothesFastR6 七項全部 valid=true／exit 0，實際經由 UI callback 連續切換 32 次，包含修改身形／左肩骨骼及不同左右手勢後第一次切到各預覽。平均 64.863ms、最短 40.428ms、最慢 179.535ms，全部低於 200ms；沒有角色重載。衣物可見三角形：穿著 25,934；由穿著半脫 19,428；由全脫再半脫 17,616；全脫 0。身體 alpha CRC 分別為穿著 1278951944、半脫 1552567581、全脫 2280405169，證明未沿用穿著遮罩。編輯參數、30 個手指目標、動畫時間 0.4 與暫停均保留，並驗證個別衣物 fallback、容量限制及失效釋放。
- 同版 ClothesFastClothesR6 十六項通過，含九個 demo 播放及七次原作衣物操作；ClothesFastHandsR6 八項通過，左右手全部原作手勢、混合、單側重設、動畫切換及換裝後設定保持正確。合計 31 項實際編輯器驗收，三份報告 exit 0，與測速使用相同 DLL。最終穿著、手勢與動作介面截圖已檢視。
- ClothesFastUnitR6 四項全部 Success：EditorClothing、EditorMods、EditorModsCoordinates、AnimationCatalog.HandShapes。ClothesFastNativeR2 三項及 ClothesFastExplicitR5 三項成功，巢狀 scope 測試重複計入兩份 log，合計五項不同案例；ExplicitR5 的另一個 portable fixture 案例未啟用、保持 ignored，不列為通過。Rust 在上述原生結果後沒有再變更。
- 成功建置 Saved/Tests/ClothesFastBuildR6.log。正式 SHA256：OpenKoikatsu `61E4C61A4047828C1E5AE42CCEB3ACD5A6E0EFB75F603B6CD38DA9F664CD77BC`；KKUnityBundle `81F63D6D04584EAB0B8093DB02344A58666790DDE8559797690114C1800E2D29`。三輪啟動、結束與最終核對皆相同。完整結果 Saved/Tests/EditorAcceptance/clothes-fast-final-r6.json；重跑入口 Tools/run_editor_acceptance.ps1 -Tag 新標籤 -Card PNG -Mode ClothesFast。
- 測速涵蓋模型／材質已準備好的常用切換，不包含首次尚未預備的原作資源擷取。R5 新增的全脫後半脫資源第一次原作擷取約五分鐘，於背景完成並持久保存為 .okstates；R6 從該持久資料正常載入。不能把這段首次資源準備宣稱為瞬間切換；背景準備不占用編輯器的 bLoadInFlight 閘門。失敗與修正詳見 E47–E51，R3／R4／R5 的失敗或中止證據均保留，速度門檻沒有放寬。

## 服裝快速切換續作核對（2026-10-08T11:05:23.9109570+08:00）

重新核對正式 OpenKoikatsu 與 KKUnityBundle DLL SHA256，均與 clothes-fast-final-r6.json 及三份實際驗收啟動紀錄一致；本功能 UI、快取、角色更新、原生狀態與驗收程式均未晚於對應 DLL。三份實際編輯器報告 valid=true、exit 0，合計 31 項；四項自動化 Success 與五項不同原生測試成功紀錄完整，一項 ignored 仍不列入通過。32 次已準備狀態切換平均 64.863ms、最慢 179.535ms，門檻維持 200ms。未發現本功能尚未建置或未完成項目；首次未準備資源仍需背景擷取的限制維持如上。本輪是既有版本與驗收證據核對，沒有冒稱重新執行完整測試；未遇到新功能錯誤，既有 E47–E51 原因、修正與失敗證據保留。

## 顏色與飾品變形／綁定階段驗收（2026-10-08 14:28 +08:00）

- 已先產生 CHARACTER_EDITOR_FEATURE_INVENTORY.md，涵蓋原作可寫欄位、控制器、已安裝模組及反編譯可辨識的 Maker 控制註冊；逐功能清單保留尚未實作／逐效果驗收項目，不宣稱所有模組功能完成。
- 卡片衍生型別化屬性：原作顏色、浮點與布林，四部位髮色、左右瞳孔、眼白／高光／眼線／眉毛、身體與服裝顏色及貼圖配置；飾品兩組位置／旋轉／縮放、55 個中文原作父節點與手動骨架名稱。新增 RGBA、HEX、色塊、中文搜尋、勾選開關、逐欄撤銷及整批套用。套用重新產生可保存的實際素材；目前不把這個需資源準備的流程稱作拖曳即時色彩預覽。
- 完整 KKEx 恢復與保留未知資料；C000 從 1573 欄恢復到 2258 欄，其中 685 欄來自模組資料。支援既有 MaterialEditor 顏色／浮點、HairAccessoryCustomizer、陰影色及可解碼模組欄位，MoreAccessories 已有 XML 變形／顏色／父節點讀寫；原始還原值與不透明貼圖資料不當作一般數值改寫。只接入欄位不等於所有模組的執行效果都已支援。
- EditorPropertiesR9 十一項通過：實際 UI 輸入、色碼、布林、材質顏色、撤銷、中文、真正模型貼圖及飾品幾何改變、另存讀回及穿著／半脫恢復。以產品 FinishPortableSave 保存回執與服裝資源後，同一另存卡在 EditorPropertiesHandsR9 八項及 EditorPropertiesClothesR9 十六項全部通過，合計 35 項，退出碼皆 0。衣物回歸包含九個 demo 動畫及七個狀態操作。
- EditorPropertiesUnitR9 五項 Success／exit 0：EditorClothing、EditorMods、EditorModsCoordinates、ChineseSettings、AnimationCatalog.HandShapes。此階段原生 EditorPropertiesNativeR8 十三項通過；後續額外欄位新增之原生 R13 十五項另記，不倒算為 R9 的二進位證據。
- 成功建置 EditorPropertiesBuildR9.log。兩個 DLL 及各次啟動雜湊、報告、退出碼記於 Saved/Tests/EditorAcceptance/editor-properties-final-r9.json。額外欄位新增與動態素材選單為下一建置，仍需獨立驗收。E52–E58 保留遇到的錯誤、原因及防止重犯方式。

## 額外飾品欄位與素材目錄階段驗收 R10（2026-10-08）

- 圖片目錄依目前卡片／服裝的 MoreAccessories XML 產生第 21 欄起的十類素材選擇。C000 原有 40 個額外欄位，因此目錄從固定 300 項變為 700 項；新增一欄後為 710 項。替換素材使用同一欄位的 GUID／resolver 身份，保留未知 XML 屬性、其他服裝及其他模組資料；新增欄位依原作使用兩組零位移／零旋轉／單位縮放與四層白色。
- MaterialEditor 既有設定加入材質／網格／屬性名稱，方便辨識與搜尋；完整著色器、貼圖匯入、額外欄位刪除／複製／鏡像及專屬物理編輯仍是盤點清單中的未完成項目。
- R10 六份報告合計 64 項操作全部 valid=true／exit 0：Properties 11、PropertiesHands 8、PropertiesClothes 16、ExtraAccessories 5、ExtraAccessoriesHands 8、ExtraAccessoriesClothes 16。額外飾品確實新增幾何，另存前後 XML 相同，24 組模組資料與七套服裝保留；兩張另存卡的 SHA256 符合其 verified.json，source_embedded=true。截圖已檢視。
- 同版 UE 五項 Success；原生 EditorPropertiesNativeR13 十五項全部通過。Native R13 後沒有 Rust 改動。成功建置 EditorPropertiesBuildR10.log；二進位雜湊及各次報告／退出碼記於 Saved/Tests/EditorAcceptance/editor-properties-r10.json。
- 流程檢查另發現新增後要重開目錄，已記錄 E59 並實作 R11 自動刷新／選中新欄位。R10 證據保留，不將其當作這個後續修正已通過的證據。


## 頭髮／飾品與詳細參數回歸 R14

EditorPropertiesHairR14（R7 DLL）13 項通過：2258 項控制、64 項分頁可遍歷及搜尋、原卡模組資料／中文標籤、待套用撤銷、RGBA、布林、材質顏色、實際顏色與飾品變形、普通髮夾編輯後假髮不可變來源／綁定與頭頂高度不變、另存讀回、半脫與穿回。假髮欄 0 的 parentKey 另存後保持原值，變形驗收使用欄 1 普通髮夾。報告位於 Saved/Tests/EditorAcceptance/EditorPropertiesHairR14/report.json。

ExtraAccessoriesSpeedR13 七項功能成功，截圖正常頭髮；穩態 300 幀平均 213.89 FPS、最低 96.46 FPS，但切換為 51,403.91 ms，100 ms 不通過。該 FPS 排除了背景預載，僅能視為穩態結果；新版驗收改為完成顯示後立即採樣。詳見 CHARACTER_EDITOR_ACCESSORY_PERFORMANCE.md。這不表示原作／所有模組功能或效能已完整完成。


## 嚴格實測 R14（R11 DLL）

七項飾品功能成功，但整體 `valid=false`、退出碼 1，兩項效能門檻都失敗：

- 點選至可見完成：37731.82 ms（要求 ≤100 ms）。
- 完成後立即採樣至背景預載結束：24,007 幀，平均 235.33 FPS、最低 2.11 FPS。
- 最長幀 474.41 ms，30 幀超過 16.667 ms。背景建立隱藏衣物角色會造成稍後卡頓，不能以平均 FPS 宣稱達標。
- 報告：Saved/Tests/EditorAcceptance/EditorExtraAccessoriesSpeedR14/report.json；保留全部逐幀數據、啟動／結束 DLL 雜湊核對及正常頭髮截圖。

EditorPerformanceUnitR12 十項全部 Success／退出碼 0，涵蓋重置、中文、衣物、模組／座標、左右手、相機、PNG 快取、貼圖 mip 及唯讀 GPU 資源重用。本輪 18 項原生、13 項 GUI 回歸、10 項 UE 單元與 7 項新增飾品功能成功；效能要求未完成，不標為完整驗收通過。

## 非同步預覽相容性（2026-10-08）

EditorPerformanceBuildR14 成功；EditorAsyncPreviewR1 實卡同步／分批載入相容性一項 Success／退出碼 0，全部 76 張載入貼圖 mip／取樣／色域一致，27 槽完成。最大單批 26.164 ms，仍未達 60 FPS 的每幀預算。PartPatchNativeR5 原生實卡一項通過，涵蓋同名網格不同欄位、保留全域與其他 renderer、空替換及毀損輸入。完整 GUI／嚴格 FPS 驗收待 R15 建置，未標為全部完成。

## 最新非同步版本 R19 驗收（2026-10-08）

EditorPerformanceBuildR19 成功。EditorPropertiesAsyncR19 的 13 項 GUI 功能通過／退出碼 0，正常頭部假髮截圖已檢視；EditorExtraAccessoriesAsyncR19 的 8 項功能通過，包含攝影機不重建與視角不變，但效能導致整體 valid=false／退出碼 1。EditorAsyncPreviewR2 的 11 項 UE 單元全部 Success／退出碼 0。原生單欄快照 helper 的 EditorPartPatchNativeR5 一項通過，尚未接入 UI。

非同步讀取與解碼、獨占 Session 的物理／模組準備、分批網格建立、逐幀材質提交、背景服裝規則均已接入。嚴格採樣覆蓋全部背景預載：點選至可見 42578.0167 ms；24,985 幀平均 213.2838 FPS、最低 8.6778 FPS、最長 115.2362 ms，83 幀超過 16.667 ms。100 ms 及持續 60 FPS 均失敗，不以非同步啟動時間或平均 FPS 代替要求。

完整證據與剩餘單欄擷取／資源重用工作記於 CHARACTER_EDITOR_ACCESSORY_PERFORMANCE.md 的 R19 段落；所有原作／模組的未完成項目仍以 CHARACTER_EDITOR_FEATURE_INVENTORY.md 為準。


## 網格重用與穩定匯出 R23

EditorMeshReuseR23：12 項 UE 測試全部通過，含跨 Session 幾何快取、資料失效、弱參照回收與原有編輯器回歸。EditorPropertiesMeshReuseR23：13 項外觀 GUI 通過，report.png 已檢視頭髮正確位於頭部。EditorMeshMorphIsolationR23：1 項實卡身體形變隔離通過。原生匯出回歸 5 項通過／4 項 ignored，另有 1 項實卡完整匯出契約通過。這些功能結果不代替嚴格切換耗時／FPS 驗收；原生單欄擷取與其他既列未完成功能仍未標成完成。

持久快取已加入有界最近使用淘汰，僅處理 session_blob／bundle_prefab，保留來源索引與現有失效條件。小容量滿庫恢復及全部 sqlcache 回歸 R24：11 項通過、2 項 ignored。跨程序讀取／淘汰競態保護補強後正在重跑 R25，UE 整合仍待建置。


## 有界快取整合 R25（2026-10-08）

最新建置成功。EditorCacheRegressionR25：11 passed／0 failed／2 ignored；EditorMeshCacheIntegrationR25：12 項 UE Success／退出碼 0；EditorPropertiesCacheIntegrationR25：13 項 GUI 功能通過／退出碼 0。已檢視 report.png：頭髮位於頭部。GUI 檢查髮色／眼線／瞳色實際貼圖、飾品旋轉／縮放／綁定、假髮來源／頭頂高度保留、保存讀回及半脫／穿回。快取在實際舊庫原地升級且維持原容量上限；真正 100 ms／持續 60 FPS 等待 EditorExtraAccessoriesCacheIntegrationR25 隔離採樣，不先標為成功。


### R25 隔離效能結果

EditorExtraAccessoriesCacheIntegrationR25 功能 8 項通過，頭髮截圖已檢視正確；啟動／結束 DLL 雜湊一致。此輪在本次其他建置／測試全部退出後啟動。切換 36684.9507 ms，23401 幀平均 200.6062 FPS、最低 9.5775 FPS、最長 104.4112 ms，140 幀超出 16.667 ms。整體 valid=false／退出碼 1，100 ms／持續 60 FPS 均未達成。原生單欄擷取仍未接入；下一變更為 E82 的隱藏預覽元件暫停／還原，待 R26 建置驗收。


### 隱藏預覽元件暫停 R26 功能驗收

EditorPerformanceBuildR26 成功（116.69 秒）。EditorInactivePreviewR26：6 項 UE Success／退出碼 0，含實卡全部 renderer 暫停、原始更新旗標精確還原、晚加入元件及 Actor 播放狀態保留、全部貼圖契約相容。EditorClothingInactivePreviewR26：7 項 GUI 通過／退出碼 0，連續 32 次預備狀態切換，最大 190.9821 ms（此驗收門檻為 200 ms，不能稱為所有切換均小於 100 ms）；穿著／半脫三角形數 25934／17616，全脫沒有可見衣物；身體遮罩 CRC 穿著／半脫／全脫為 1278951944／1552567581／2280405169。編輯身形／骨架、獨立左右手、動畫時鐘／暫停、快取上限與失效釋放均通過；report.png 已檢視正常。

隱藏角色只暫停自有 MeshComponent，記錄原始旗標，分批新建後再次納入，取出快取前還原；不修改全域動畫或 GC 設定。飾品嚴格效能 EditorExtraAccessoriesInactivePreviewR26 正在所有其他本次測試／編譯退出後單獨執行，尚未宣稱 100 ms／持續 60 FPS 已達成。


## R26 最終驗收結果（2026-10-08）

EditorExtraAccessoriesInactivePreviewR26：8 項飾品功能通過；頭髮位於頭部，report.png 已檢視。啟動／結束二進位雜湊核對成功。本次其他建置及測試全部退出後才啟動，採樣從可見替換完成持續到服裝規則、原生工作、分批建立與所有材質就緒，未刪除長幀。

| 指標 | R26 實測 | 要求／判定 |
|---|---:|---|
| 點選至可見完成 | 36880.3430 ms | ≤100 ms，失敗 |
| 完成後採樣 | 20,029 幀 | 含全部背景預載 |
| 平均 FPS | 200.8508 | 不代替最低 FPS |
| 最低 FPS | 16.3567 | ≥60 FPS，失敗 |
| 最長幀 | 61.1369 ms | 超出 16.667 ms |
| 超出幀預算 | 39 幀 | 持續 60 FPS 未達成 |

報告：Saved/Tests/EditorAcceptance/EditorExtraAccessoriesInactivePreviewR26/report.json、report.png、launch.json、exit.txt；整體 valid=false／退出碼 1。R25 的超預算 140 幀／最長 104.4112 ms，本輪為 39 幀／61.1369 ms；僅記錄單次觀察，不以此宣稱穩定改善或通過。

本輪已驗證的正式功能：完整內容指紋之有界弱參照骨骼網格重用、穩定骨架匯出、容量滿後可恢復寫入的持久快取淘汰、隱藏衣物預覽的顯示元件暫停與精確還原。原生快取 11 項、UE R25 12 項、外觀 R25 13 項通過；R26 修改後另有 6 項 UE、7 項衣物 GUI／32 次切換及 8 項飾品功能通過。未將 ignored、失敗效能或舊版案例當成最新版全部功能通過。

100 ms／持續 60 FPS 仍未完成。原生背景衣物開啟實測 28–35 秒，shared_edited_outfit 仍完整擷取目前套裝，UE 仍建立整套預覽元件。下一必要工作是共用真正單欄擷取路徑並接入已有快照 patch：保留原始身體／頭部貼圖池、選定髮光澤、原作選項／採樣及完整參考骨架；不能以合成後的皮膚貼圖近似來源。再以來源欄位識別替換 renderer，正確重映射索引並保留其他元件、材質、攝影機與動作狀態。既有單欄快照 helper 的測試通過不代表此生產路徑已接入。其他原作／模組尚缺功能仍以盤點表為準，未宣稱全數完成。

## R27 單欄來源更新（2026-10-08）

飾品正式 apply 已接入共用單欄來源擷取及 portable patch；保留原始貼圖池、骨架、材質／位移／綁定契約、其他部件與服裝。舊卡資料不足時完整恢復，未用名稱猜測或合成貼圖近似。此項取代 R26「來源單欄尚未接入」的待辦狀態；UE 顯示元件保留及最終 100 ms／持續 60 FPS 仍未完成。

原生 17 項通過（另 5 項明列 ignored）、新版建置成功、UE 12 項／外觀 GUI 13 項通過；衣物隔離驗收 7 項／32 次切換通過，最長 165.0008 ms，不等於小於 100 ms。飾品新舊卡各 8 項功能通過，嚴格效能失敗均保留。詳細原始報告、來源解析／衣物狀態指紋修正、環境及剩餘通用改造見 [R27 驗收](CHARACTER_EDITOR_SINGLE_SLOT_R27.md)。錯誤 E83–E91 記錄於 CHARACTER_EDITOR_ERRORS.md。其他原作／模組盤點缺項尚未宣稱完成。

R27 最終單欄隔離驗收 EditorSingleSlotExtraIsolatedR27：8 項功能通過，頭髮截圖已檢視；點選至可見 4906.1713 ms，30832 幀最低 13.6358 FPS、最長 73.3365 ms、15 幀超預算，整體失敗。平均 371.6902 FPS 不代替最低幀率。來源單欄已完成，但 UE 顯示／保存／背景預備的完整效能仍未完成；詳 CHARACTER_EDITOR_SINGLE_SLOT_R27.md 與錯誤 E92。

## R28–R30 背景交易與來源重用（2026-10-08）

R28 已將外觀／詳細設定的卡片保存、原生請求及輸出讀回放入同一背景交易；輸出解析成功且選角／套裝版本仍一致才發布。新增真實卡片交易及失敗不提交測試。建置、13 項 UE 回歸、13 項詳細設定 GUI 均成功；32 次服裝切換功能成功，最大 155.8938 ms，仍不等於 100 ms。詳 [R28](CHARACTER_EDITOR_ASYNC_CARD_R28.md)。

R29 已完成有檔案通知失效機制的四目錄有限不可變原作清單快取；通知不可用、不完整讀取或載入中變動均不發布快取。1,851 筆真實清單全部欄位精確一致；原生 17 項不同測試及 UE 13 項成功。飾品 8 項功能成功，假髮位置正確，但點選至可見 5281.9743 ms、最低 6.19176 FPS，嚴格效能失敗。詳 [R29](CHARACTER_EDITOR_CATALOG_REUSE_R29.md)。

R30 正以 CPU 時序定位並移走遺漏的骨架參考資料懶建立工作，已實作背景預備及完整資料契約，正在建置及驗收。診斷追蹤不能取代關閉追蹤的正式逐幀結果；不能以平均 FPS 宣稱持續 60 FPS。原作／mod 盤點中其他缺項仍未宣稱完成。詳 [R30](CHARACTER_EDITOR_TIMING_R30.md)。

## R30–R33 最新驗收狀態（2026-10-08）

R30 背景骨架參考預備已完成建置及 14 項 UE 測試；正式未開追蹤的飾品功能 8 項成功，切換 7212.3536 ms、最低 19.8483 FPS，效能未達標。R31 補正首次載入模式判定並將單欄更新限縮為部件資源序列化，真實來源 34 部件、640 資源全部精確一致；序列化節省僅約 1–3 ms，不能當作已解決主要瓶頸。詳 CHARACTER_EDITOR_TIMING_R30.md、CHARACTER_EDITOR_PART_SERIALIZATION_R31.md。

R32 跨工作執行緒的遷移快取完成：原生 33 項不同測試、UE 15 項、詳細設定 GUI 13 項、衣物 GUI 7 項／32 次切換及飾品 GUI 8 項功能皆成功。詳細設定截圖確認假髮在頭部，顏色、綁骨架、旋轉縮放、保存及半脫／恢復已實際操作。衣物最大 184.9502 ms；飾品切換 5592.725798 ms，27611 幀最低 6.047708 FPS，165 幀超預算，嚴格效能仍失敗。測試期間另有 E 槽 UnrealEditor，保留環境資訊，不宣稱隔離或穩定改善。詳 CHARACTER_EDITOR_MIGRATION_REUSE_R32.md。

R33 已完成來源通知外部連結失效測試及圖片目錄背景遷移預備；新增 RHI fence 以確認資源就緒，正在進行新版建置及實際渲染測試。原生 17 項模組、13 項清單及 4 項來源／卡片契約通過，3 項 ignored 與 1 項 ignored 分別保留；安裝來源 91 GUID 冷讀 1555.072 ms，通知驗證重用平均 0.057 ms。這是來源快取指標，不能代替整體切換時間。100 ms／持續 60 FPS 和盤點中的其他原作／mod 缺項尚未完成，不宣稱全功能驗收成功。詳 CHARACTER_EDITOR_SOURCE_PREPARATION_R33.md，錯誤紀錄至 E109。

### R33 完成結果

34 項不同原生測試、16 項 UE 整合測試、13 項詳細設定 GUI 及 8 項額外飾品 GUI 功能成功；建置 120.55 秒。兩份 GUI 截圖確認假髮位置正確。衣物完成 32 次切換與 5 項功能檢查，但最大 212.9688002 ms，因速度斷言失敗停止，後續兩項未計通過。

飾品切換 6446.7418976 ms，25,543 幀最低 3.7552381 FPS、最長 266.2946992 ms，141 幀超預算，整體失敗；平均 187.5677081 FPS 不取代最低值。啟動／結束無其他 UE 的兩份環境快照、二進位雜湊、來源 receipt、背景交易時序及完整幀資料都已保存。RHI 就緒及外部連結來源失效修正已完成，100 ms／持續 60 FPS 未完成。詳 CHARACTER_EDITOR_SOURCE_PREPARATION_R33.md，錯誤至 E113。

R34 已列計畫並實作五階段初始化及各階段取消／重啟測試，正在建置；尚未當作驗收成功。詳 CHARACTER_EDITOR_INCREMENTAL_SETUP_R34.md。

### 最新效能要求（2026-10-08 使用者更新）

每個飾品點選至可見的門檻由 100 ms 調整為 300 ms；切換後持續至少 60 FPS，仍包含背景預備期間。新報告明列 switch_budget_ms=300、switch_budget_pass、switch_300ms_pass；原 switch_100ms_pass 保留原本 100 ms 意義作歷史比較，不改寫舊報告。此變更沒有調整衣物驗收的既有 200 ms 門檻。


### R34 初始化拆分驗收

新版成功建置（113.55 秒），17 項 UE 整合測試成功，包括五階段取消／重啟、渲染就緒與實際同步／增量貼圖資料一致。正式衣物 7 項／32 次切換成功，最大 175.6240986 ms；診斷標籤另保存，不混作正式數字。

正式飾品 8 項功能成功，假髮截圖已檢視；報告使用使用者更新的 300 ms 門檻。切換 5186.2038001 ms，27,008 幀最低 5.1937502 FPS、最長 192.5391033 ms、38 幀超預算，整體失敗。環境啟動快照無其他 UE，結束快照出現 E:/PhysX UE，不能宣稱完整期間隔離，亦不直接歸因外部程序。完整報告、二進位雜湊及時序保留於 EditorIncrementalSetupExtraAccessoriesR34。

R35 已先量到 7,316 ZIP 清單每次重掃約 53–72 ms，再實作有通知失效及連結保守回退的跨執行緒快取，原生測試進行中。未將此項快取命中當作 300 ms／持續 60 FPS 已完成。詳 CHARACTER_EDITOR_INCREMENTAL_SETUP_R34.md、CHARACTER_EDITOR_INVENTORY_REUSE_R35.md，錯誤至 E115。

### R35 正式驗收結果

50 項不同原生測試及 17 項 UE 整合測試成功。外觀 Properties 13 項、衣物 ClothesFast 7 項／32 次切換成功（最大 198.4203979 ms）。三次 GUI 的兩個 DLL 雜湊皆與啟動紀錄一致，未開 CPU 追蹤；截圖均已檢視，假髮位於頭部。

EditorInventoryExtraAccessoriesR35 的 8 項功能成功，但效能失敗：switch_budget_ms=300、switch_budget_pass=false、switch_300ms_pass=false；切換 5154.4246003 ms，34,396 幀最低 6.1119539 FPS、最長 163.6138000 ms、138 幀超過 16.667 ms。平均 204.6888788 FPS 不能替代持續 60 FPS。完整背景期間未排除。來源 receipt 證實只重建第 20 飾品一個部件、重用 34 部件及其他 6 套服裝；來源擷取 437.9242 ms，本身已超過 300 ms。工作執行緒原生請求 968.9758 ms，輸入另存 241.5384 ms、結果讀取 177.0170 ms，總可見切換仍 5.154 秒。

驗收啟動有外部 E:/PhysX UE，結束沒有；此為兩個時點快照，不能宣稱全程隔離或將失敗歸因於外部程序。exit.txt 為 0，但 report.valid=false，正式 PowerShell 驗收據此正確拋出失敗；不以程序退出碼掩蓋未達標。來源 receipt、交易時序、完整逐幀報告保存在 Saved/Tests/EditorAcceptance/EditorInventoryExtraAccessoriesR35。300 ms 門檻更新完成；目前效能仍未達標，不標記全部需求完成。

### R37 角色卡改成連續捲動

移除角色卡清單分頁，使用填滿剩餘視窗高度的虛擬化 UListView 與正式列 Blueprint。可見列單一背景工作補名稱，不讀取整目錄大型卡片。EditorCardScrollFinalGuiR37 實際 100 張、5 項 GUI 成功，最後 C099 可視、可選、可載入 3D，能返回頂部；截圖已檢視，詳 CHARACTER_EDITOR_CARD_SCROLL_R37.md。這是清單與底部可操作性驗證，不代表先前飾品 300 ms／持續 60 FPS 已通過。

EditorPropertiesColorR3（2026-10-18，E: duo，C000 OK 卡）：15 項全部 valid=true。項目包含新增的物理下拉兩項（`physics_solver_uses_choice_with_default`、`physics_choice_saved_in_card`）。OK 卡套用後預覽確實重載（run.log 有第二筆 `OK load3d`）。顏色檢查涵蓋貼圖與著色器顏色；一般飾品由卡片資料自動挑選。report.png 已檢視：假髮在頭上，髮夾移到脖子。原因與修正見 CHARACTER_EDITOR_ERRORS.md「OK 卡套用並更新角色後預覽沒換」。
