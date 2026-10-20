# 角色編輯器錯誤與防止重犯（2026-10-08）

## E01 驗收範圍不足

先前 Demo 畫面測試只覆蓋 67 個基本動作及各額外控制器的代表狀態。739 個可用 Demo 的目錄解析成功，不等於逐項畫面播放成功。修正：完整模式逐項選擇與等待真實動畫樣本，記錄每一個來源限定 key、duration、sample。抽查／單衣服模式明確分開，不混稱全驗收。

## E02 同插件未知欄位保留

ABMX 寫入原先整個替換插件 data map，只保留 boneData，可能遺失同插件額外欄位。已修正：只更新已知 boneData，保留未知欄位與擴充陣列。對不支援的資料結構返回錯誤，不覆寫。帶有未知二進位欄位的原生 round trip 已通過。

## E03 Windows 搜尋路徑

曾把 `Tools/ok-extract/src/*` 或 `Saved/Tests/EditorDemo*.json` 當 PowerShell 會展開的 rg 路徑，得到 os error 123；亦曾猜錯插件資料夾。防止重犯：先用 `rg --files <真實目錄> -g <pattern>` 找到檔案，再讀精確路徑；搜尋內容直接傳目錄，不使用路徑尾端 shell glob。本輪仍重複發生 OKPaired*.cpp、*Camera* 尾端 glob 與 resource_tool.rs 猜路徑，已保留工具錯誤。修正後 Camera 內容搜尋直接傳 Tests 目錄及 -g '*.cpp'，成功定位 OKCameraControls.spec.cpp；不可因壓縮摘要而跳過此規則。

## E04 UI 修改中的字串替換

先前廣泛替換 `Box->`、`Label->`，誤匹配 `LabelBox->`、`EnabledLabel->` 尾端，造成 C2065。已修正且正式建置通過。防止重犯：使用精確上下文／完整識別符；修改後立即檢查差異並編譯，不把文字替換成功當成程式成功。

## E05 UI 換行與更新方式

長標籤只有 AutoWrapText 而沒有確定 wrap 寬度和列高，造成行間重疊；重置文字不應換行。已使用明確寬度及最小列高，重置文字不換行。Pregnancy+ 先前重建整個角色及材質，後改為更新骨骼 mesh／私有衣服來源 revision，保留播放與材質。畫面與動畫／換衣回歸需一起驗證。

## E06 首輪服裝座標測試錯誤

EditorAcceptanceUnit 初輪 3/4 PASS，EditorModsCoordinates 的「Outfit 1 unchanged」FAIL：測試錯把初次修改第 0 套後的第 1 套預期設成 1.2。實際通用流程先建立各套中立值 1，再只改選中的服裝；第 1 套正確應保持 1。修正測試預期，保留初輪失敗 log，不改正確的座標隔離實作來迎合測試。

## E07 插件版本的不同儲存脈絡

審查時一度將 ABMX 的服裝卡 ReadCoordModifiers / version 3，誤認為角色卡的版本。讀完整 ReadModifiers / SaveModifiers 後確認角色卡 list 是 version 2，legacy 字串格式是 version 1；服裝卡則使用不同版本。未將錯誤假設寫入實作。防止重犯：判斷 schema 必須配對同一儲存入口的讀／寫兩端，不能混用角色卡與服裝卡。

## E08 單側缺失模型的選擇器

若整列以選中側是否存在而停用，只有一側骨骼的自訂模型可能無法切換至存在的側。修正：列是否可用按任一側存在判定；滑桿與重置按當前所選側判定，側選擇器維持可操作。完全不存在的骨骼仍停用。

## E09 全 Demo 驗收揭露背景雙人預載爭用

FullDemoR1 未完成且不是 PASS。約 113 個已播放 log 後，單人編輯器的背景配對目录在沒有配對角色時預載 99 組，前幾組耗時 60.727608 / 22.722255 / 23.865125 秒，與選中 Demo 的來源解碼爭用快取；單人切換 worker 出現 46615.800 / 21117.080 ms 等待。Primary 核對自己程序 PID 71816 的完整 CommandLine 含 FullDemoR1 / ok.AuditEditorDemo 後終止，保留 log 與 incomplete report。修正：沒有 PartnerActor 時不啟動配對預載；載入配對角色／進入互動時走原本恢復入口。不是測試專用開關，不停用其他程序。重新從第一項執行全清單。

## E10 編譯完成前啟動驗收

EditorAcceptanceBuildR2 尚在 link／WriteMetadata 時就啟動 FullDemoR2、ModsC001R2 與 UnitR2，讀到 engine／project 不一致的 BuildId（09b78e63... vs 02dd9ef1...），插件載入失敗。這些不是功能測試 PASS；保留 startup log。BuildR2 最終 exit 0 / Succeeded，metadata 已一致。修正：明確等待建置完成才啟動；通用驗收腳本新增 BuildId 一致性檢查，沒有 report 時回報 exit 與原 log，而不是不明確的 Get-Content 錯誤。不更改或停用插件以迴避版本錯誤。

## E11 驗證工具參數

verify_shape_defaults.py 的 game 是位置參數，誤用 --game 造成 argparse error，沒有修改或驗證資料。已改用文件／parser 定義中的位置參數重跑，保留初次錯誤 log。執行既有工具前先讀其參數或 --help。

## E12 同動畫包多控制器重複目錄解碼

FullDemoR3 仍是 incomplete，不當成全項 PASS。背景配對已延後，但不同 controller owner 的單次 worker 還有約 11–23 秒；cached_bundle_entries 以 source.key（包含 owner）當 key，而 entries_in_bundle 只掃描完全相同的 base payload、已包含所有 owners。因此每個 owner 都重掃全包 clips。修正：原始片段目錄 key 改用既有 payload_key；controller / overlay / layer graph 與實際 clip 的獨立 key 保持原規則。Primary 只停止 CommandLine 經核對含 FullDemoR3 / ok.AuditEditorDemo 的自己測試程序，保留 partial report 與 log，重新全項播放。用同包不同 owner 的原生回歸及完整 Demo 驗收確認沒有錯配。

## E13 共用引擎 DLL 被其他程序占用

BuildR4 的根專案與 KKUnityBundle 已 link，但 UnrealEditor-NetCore.dll 被 UnrealEditor-Cmd.exe 占用，UBA retry 20 秒後 LNK1104；整個建置 FAIL，不啟動驗收或冒稱成功。沒有終止其他工作。檢查時該程序已結束，使用正常官方建置重試，等待 metadata 完成。錯誤 log 保留在 EditorAcceptanceBuildR4.log。

## E14 驗收來源檔路徑

摘要僅列檔名時誤將 Tests 下的 OKEditorDemoAudit.cpp 當成模組根目錄，讀取失敗，沒有修改資料。之後亦猜錯 ok_extract.h、KKCardTypes.h；實際標頭以 rg --files 定位為 Tools/ok-extract/include/ok_extract.h。往後沿用已確認的完整路徑，禁止由檔名推測資料夾。

## E15 單件服裝切換未遵循原生連動

FullDemoR5 播放 739 項與原有 7 項斷言 PASS，但人工檢查報告發現 top_state=3 的單件上衣仍顯示與穿著相同的 25,934 三角形。不能把原有斷言 PASS 當成功能完整。原因：UI 只修改上衣，對連身衣的 notBot 規則，原生 UpdateClothesStateAll 接著套用仍為 0 的下裝，使連動上衣恢復穿著。

修正：共用原生 SetClothesState 的合法狀態／連動演算法，先用實際原版或 mod 服裝清單、DEF 參考與 NotBra 子部件取得規則，再產生完整一致的狀態向量。規則按 portable 資源 digest／服裝座標儲存，另存時複製，支援首次準備後離線使用；不以角色名、三角形差異或猜測配對修補。全驗收新增「單件上衣 state=3 時 clothes_top 可見幾何為 0」及 bottom_state 記錄，保留 R5 舊報告，最新建置重跑。

## E16 原生連動測試的錯誤期待

初次規則測試把單件半脫期待為 [1,1]，實際原生 SetClothesState 是 [1,0]；只有全脫才強制連動另一件為 3，穿回則恢復原為 3 的連動件。原生程式未改動；修正測試期待為來源規則，保留 EditorClothingRulesR6.log，重新驗證。修改驗收程式時首次 apply_patch 只提供半行，找不到完整上下文，未寫入；改用完整原行後成功。

## E17 舊換衣測試缺少角色資料

UnitR7 的 EditorClothing / EditorMods / EditorModsCoordinates / ShapeReset 通過，但 AnimationLookup 內附的衣服測試仍用沒有 Entries / SelectedIndex 的空 Widget。新的狀態規則必須來自實際服裝，空物件正確拒絕切換，舊斷言因此失敗。不是動畫 lookup 錯配，也不以無資料的通用猜測恢復舊行為。改為 C000 實際 portable fixture 後重跑。FullDemoR7 正在驗收同一份正式服裝程式；只修改測試來源，等其結束後再建置測試修正，避免中途替換 DLL 使完整報告無效。

## E18 已穿著角色的正常重用被驗收誤判

FullDemoR7 的 739 個 Demo、全脫與單件連身上衣全脫斷言均通過，單件全脫上衣三角形為 0、下裝同步為 3。終端報告仍為 FAIL：第 7 項是已穿著狀態再次「穿上」，正式編輯器正確重用當前 actor，因此 bLoadInFlight=false，舊測試錯誤要求每次必須重載。

修正驗收而不移除正式重用：先比對 callback 套用的九項狀態是否符合原生規則，再等待有需要的 reload 完成，最後驗證實際衣服幾何及動畫；合法 no-op 同樣驗證顯示與播放。新增明確 Clothes 模式（9 Demo + 7 衣服）先快速覆蓋整個狀態序列，再用 Demo 模式重跑 739 + 7；不將 R7 的終端 FAIL 改寫為 PASS。

## 最終確認

BuildR8、UnitR8 五項、ClothesR8 十六筆、三角色 R8 各五項，以及 FullDemoR8 739 動作 + 7 衣服（746 筆）全部 PASS、exit 0。完整驗收前後 DLL 雜湊一致。E15 的單件連身全脫上衣幾何為 0、下裝同步為 3；E17 真實卡 fixture 及 E18 正常 no-op 都在最新版本通過。原生規則另存／移除來源紀錄後離線測試也通過，所有失敗輪次保留。

## 既有啟動診斷

正常預覽啟動仍有 CommonUI / CommonGameViewportClient 的既有 LogUIActionRouter 診斷，舊驗收輪次亦有同訊息；本次未修改該路由設定。編輯器正常啟動、Responding=true、3D 載入 ok=1，當前功能驗收均完成。不可把這個既有訊息歸因於新增重置／參數／動畫／服裝功能，也不可把功能 PASS 解讀為已修正 CommonUI 設定。

## E19 最終摘要腳本的位置參數

Select-String 與 -SimpleMatch 混用位置參數，將載入成功字串解讀成路徑；摘要寫入前即停止，未改變程式或驗收結果。已改成明確 -Path / -Pattern / -SimpleMatch，重新確認正常預覽後保存摘要。含多個字串的工具參數須明確命名。

## E20 驗收表格殘留舊計數

續作核對發現儲存再讀一列仍寫「最新四項回歸」，但 R8 實際包含新增 EditorClothing，總數為五項。已依 Automation Test Queue Empty 5 tests performed 及五項 Success 更正。更新測試集合時，所有引用總數的文字須與最終原始報告一起核對，不能只更新總表。

## E21 拖曳時更新被 debounce 持續延後

新增參數使用「距離最後一次輸入 0.15 秒」作為套用條件。連續拖曳不斷改寫時間，因此直到停手才更新身體。改為 NativeTick 每畫面合併最新值並套用，不依賴放開事件；骨骼與 Pregnancy+ 共用此流程。新增實際編輯器持續每畫面送值、期間檢查原生骨骼及 GPU mesh 已改變的驗收，不能用手動 ApplyPendingEditorMods 掩蓋更新排程問題。

## E22 驗收程式 TObjectPtr 的迴圈型別

首次 R9 編譯時以 auto* 遍歷 TArray<TObjectPtr<UOKShapeSlider>>，編譯器無法推導自動指標轉換。已改為明確 UOKShapeSlider*；遍歷 UObject 包裝指標陣列時採明確型別或 .Get()，避免重複犯錯。原始失敗保留於 EditorDisplayLiveBuildR9.log。

## E23 PowerShell 的 rg 萬用字元

rg 接收 Source/OpenKoikatsu/OKGallery*.cpp 在 Windows 不會由 PowerShell 展開，產生路徑語法錯誤。改以目錄搭配 -g 'OKGallery*.cpp'；不把無結果視為程式不存在。

## E24 回歸測試篩選器命名空間

R10 第一組自動測試錯用 OpenKoikatsu.Gallery.AnimationLookup，實際名稱是 OpenKoikatsu.Motion.AnimationLookup，因此僅執行四項。已由測試宣告核對，另外執行正確名稱並成功；最終數量應為四項 + 一項，共五項，不可只看程序 exit 0 宣稱所有指定名稱都執行。

## 版面配置注意事項

UScaleBoxSlot 預設水平／垂直置中，會以內容期望高度配置長參數列表。編輯欄縮放必須明確設成 HAlign_Fill / VAlign_Fill，讓捲動欄得到縮放後的可用寬高；不能只縮放文字而讓按鈕、換行、滑桿點擊區域重疊。

## E25 開發版設定目錄不能猜測

最終查驗曾讀取 Saved/Config/Windows/GameUserSettings.ini，但目前開發版實際使用 WindowsEditor。已用 rg --files Saved/Config -g GameUserSettings.ini 找到檔案，確認 EditorDisplay 的 PanelWidth=360、FontSize=16 已還原。正式功能一直使用引擎 GGameUserSettingsIni，未硬編碼平台目錄；後續驗收亦應先查實際路徑。

## E26 產生的參數名稱仍是英文

先前骨骼控制直接顯示 ABMX 來源 Label，造成肩部、乳頭、四肢與手指等大量設定沒有中文。改在 generate_bone_controls.py 套用 editor_bone_terms_zh_tw.json，1192 個顯示名稱全部產生為繁體中文，未知英文術語直接報錯，禁止靜默漏翻。Category／Bone／Right／Axis／範圍等內部資料保持來源值。Pregnancy 預設新增獨立 Label 欄位，來源 Name 與 JSON 不翻譯，避免翻譯破壞查表或存檔相容性。中文分類比較與實際編輯器驗收同步更新。

## E27 Windows Python 主控台編碼

掃描中文字串時預設 cp950 不能輸出部分簡體字，發生 UnicodeEncodeError；只影響診斷輸出，UTF-8 原始碼未損壞。改用 python -X utf8 執行中文字串診斷，所有檔案繼續明確使用 UTF-8 讀寫。不可把主控台亂碼當成原始碼已損壞而重寫內容。

## E28 執行中的讀卡預覽啟用 Live Coding 阻擋建置

補上動態動畫參數中文顯示時，第一次 R12 建置被引擎 Live Coding 閘門拒絕，正式 DLL 未被替換。查明當時是 Saved/Logs/CardReader/20261008-085934175-50c1b943.log 對應的讀卡遊戲程序；未強制結束此不屬於本輪驗收的程序。確認其正常退出後重試，EditorChineseBuildR12Retry.log 建置成功。後續必須在成功建置完成後才啟動新一輪驗收，並比對 DLL 雜湊；不能以先前版本報告充作新版本結果。

## E29 圖片選擇不能把 Sprite 當成 Texture2D

原遊戲的 ThumbTex 通常是 Sprite 名稱，首次 Texture2D 讀取只有 1/32 張眉毛縮圖成功。新增依 Sprite 的 texture、textureRect 與 settingsRaw 讀取圖集、裁切及還原方向，眉毛第一頁 32/32 成功；保留模組 PNG 和獨立 Texture2D 支援。不得以假圖或不相關紋理掩蓋缺圖，缺圖明確顯示「素材未提供縮圖」。Rust 首次編譯遇到 Cursor 未引入和 objects_of_type 回傳 Vec 而非 Iterator，已按實際 API 修正。

## E30 圖片套用必須保留完整卡片與模組識別

圖片類型不能只改 UI 中的編號或沿用舊 portable 快照。修改原始 MessagePack 欄位並重建所有服裝的獨立資源；保存原卡未知欄位、KKEx 與區塊表版本。配件的 resolver property 必須是 outfitN.accessoryN.ChaFileAccessory.PartsInfo.id；原生遊戲與各 zipmod 以來源＋分類＋編號建立獨立鍵，禁止合併同編號模組。KKEx 鍵含句點，不能套用巢狀欄位查找。支援舊版 resolver key，保留其他模組資料。300 個欄位真卡檢查通過。

## E31 建置與 UTF-8 診斷

首次 Python 讀檔未指定 UTF-8，在 cp950 環境失敗且未執行修改；後續明確使用 encoding='utf-8'。另一個建置程序占用 UnrealBuildTool 全域 Log.txt 時，-WaitMutex 之前就可能在日誌備份失敗；重試使用本輪專屬 -Log 路徑，不終止其他工作程序。新的原始碼修改必須等對應建置成功後重新驗收。

## E32 搜尋驗收事件與獨立妝容

R4 實際驗收目錄與縮圖通過，搜尋測試失敗。UE 的 EditableTextBox::SetText 不會送出使用者 OnTextChanged 事件，驗收改為設定文字後廣播真實輸入事件。不得移除搜尋斷言充作通過。原遊戲 CvsMakeup 對 enableMakeup 服裝使用獨立妝容，圖片選擇因此依選中服裝狀態修改 Custom 或 Coordinate；不能只改 baseMakeup 造成畫面不變。配件重置需同時還原 type=120，不能保留原分類、單改 id=0。

## E33 舊版精簡獨立卡缺少 KKEx 導致未修改部位重建不同

R5 七套服裝重建與外觀回讀成功，但觀察到身體頂點數由 9558 變成 9823；因此不能只用「畫面出現、編號改變」判定成功。舊 lean 卡只有 Custom／Coordinate／Parameter，重新從遊戲載入時缺少原始模組、材質編輯器與身體替換來源。已中止本輪擁有的驗收程序，新增通用來源還原：以完整外觀欄位（排除使用者會修改的身形／身體物理）及全部服裝精確比對遊戲原始卡，只有模組資料唯一相符才還原 KKEx，保留目前編輯的數值和已存在的模組資料。新外觀卡保留完整 KKEx 與來源標記，後續改圖不再依賴重新尋找原卡。來源不明或有歧義時保留目前角色，禁止按名稱或重複編號猜測。實際 UI 驗收新增未修改身體網格頂點數一致的斷言。

## E34 原始卡索引不可讀取整個圖片與材質資料

安裝的 UserData/chara 有 13047 張卡、約 120 GB。首版來源索引整檔讀取，使單一測試長時間執行；已結束本輪該測試程序，改用檔案 seek 跳過 PNG 圖片和 KKEx 大型材質資料，只讀 Custom／Coordinate 小區塊。索引以檔案路徑、大小及修改時間失效，完成後以暫存檔原子發布。不得把檔名當成角色來源，也不得為加快搜尋而任意挑選第一個相同編號。驗收使用原始卡產生的單套服裝工作副本縮短重複點選成本，正式七套服裝卡仍保留全部服裝，原始資料不覆寫。

## E35 舊卡來源須同時比對不可變身份與完整外觀

同外觀的舊版／新版原卡可能具有不同 KKEx，R8 原生測試因此拒絕歧義來源。來源索引加入 Parameter 的 lastname、firstname、sex 與全部外觀／服裝共同精確比對；nickname、使用者調整的身形不參與識別。這是卡片資料身份比對，禁止依檔名猜測。R9 七項原生測試通過，原始身體替換模組恢復且目前數值保留。

## E36 圖片編號改變不等於實際材質改變

Appearance R8 實際編輯器驗收通過縮圖、搜尋、點選、磁碟回讀及未修改身體頂點一致，但最終貼圖 CRC 未改變：MaterialEditor 儲存的眉毛 MainTex 覆寫了新選項。新增按部位、服裝座標、槽位及貼圖屬性清除衝突覆寫，保留其他身體／材質資料。幾何種類更換沿用原 MaterialEditor maker 的槽位重設範圍。不得刪掉貼圖變化斷言或清空所有模組資料。首次 R10 編譯誤用 rmpv::Value 不存在的 as_map_mut/as_array_mut，已改用 Value::Map／Value::Array 模式匹配；後續七項原生測試全通過。

## E37 新外觀卡的服裝狀態不得依賴暫存原卡

重新產生外觀卡後必須同步產生 verified receipt，否則穿著預覽可用但首次準備半脫／全脫失敗。新卡保留完整原始資料，receipt 以 source_embedded、game_root 及完整 PNG SHA256 識別；服裝準備從卡內重建 source 至不可變資源狀態快取。儲存／搬動卡片沿用既有 receipt 雜湊更新與快取複製機制，避免依賴 Saved/AppearanceEditor 暫存檔。舊 receipt 仍保留來源雜湊檢查。實際驗收必須更換圖片後再測半脫、全脫、穿回的幾何結果。

## E38 記錄檔應使用明確的專案絕對路徑

本輪在 Tools/ok-extract 工作目錄以 Docs 相對路徑追加紀錄，Python 拒絕且未寫入檔案。改用專案絕對路徑後完成紀錄；呼叫工具時須核對 workdir，不把不同工作目錄的相對路徑混用。

## E39 上下眼線與衣服多圖層必須按實際材質命名處理

檢視通用材質清除條件時發現，下眼線可能使用 _low 或 down，不能只排除 down；下眼線選擇也不能用「不是 up」把無後綴的上眼線一併清除。改為明確識別下眼線別名，新增原生測試保留下眼線與身體覆寫。衣服花紋／徽章會烘焙至 MainTex、MainTex02、MainTex03 等圖層，需清除所選服裝槽位的對應 MainTex 系列衝突；瞳孔選擇保留獨立高光 OverTex。實際驗收補上模組上眼線套用前後貼圖 CRC 不同與套用後截圖，不只檢查 GUID。

## E40 驗證工具須使用完整來源清單與明確參數

首次執行 verify_editor_controls.py 沒有必需參數而被 argparse 拒絕；隨後誤把 BoneMeta.cs 型別定義傳入，僅解析 75 項，因此精確比對拒絕，不修改正式控制項。正確 ABMX 來源是 KKABMX.GUI/InterfaceData.cs，Pregnancy+ 是 KK_PregnancyPlus/BellyTemplate.cs；明確傳入後 1192 個控制項與 11 預設及中文覆蓋全數通過。Windows PowerShell 不展開 rg 的位置參數萬用字元，應以明確目錄搭配 -g 指定檔案篩選，避免無效路徑。

## E41 實際截圖須等候材質編譯與渲染穩定

Appearance R12 模組貼圖 CRC 已改變且 GUID 驗證通過，但套用後立即截圖捕捉到 Preparing Shaders 階段，角色暫時發黑。這不是穩定顯示的有效視覺證據。驗收改為等待 GShaderCompilingManager 完成，且非忙碌至少兩秒讓 SceneCapture 更新，再截圖與後續操作。不得以著色器準備中的截圖宣稱最後畫面已驗收。

## E42 同 GUID 的重複 zipmod 不能顯示成兩個可獨立套用來源

安裝內容有 12 組 GUID 各出現在兩個 zipmod；若圖片目錄全部列出，卡片 resolver 仍只會依 GUID 載入原生索引選定的來源，可能出現選第二份卻載第一份。目錄現在依與實際載入相同的 find_guids 索引保留有效來源，每次讀取快取仍核對；不同 GUID 的相同編號繼續獨立顯示。新增來源優先序與路徑大小寫測試，避免靠名稱或手動排除指定模組。診斷列印含非 cp950 字元時曾被主控台拒絕，後續 JSON 診斷採 ensure_ascii=True 或 UTF-8 檔案。

## E43 複合命令須檢查前一步失敗，避免把舊測試結果當新修改證據

在原生子目錄的複合命令中再度使用專案相對路徑，Python 在修改前拒絕，但 PowerShell 繼續執行 cargo，R14 因此只跑到舊八項測試，不是重複 GUID 修正的證據。改用明確絕對路徑與獨立 apply_patch／測試步驟；apply_patch 遇到不完整行比對也會原子拒絕，修正 hunk 後重試。只有包含新案例的 R14Retry 九項測試才可作為修正證據。

## E44 左右手底層存在不代表編輯介面容易找到

本次查到手勢載入、左右手獨立骨骼覆寫與甲乙混合已存在，但 UI 位於語音／表情後的長捲動欄底部，沒有直達入口。本輪將控制移到動作欄頂部、左欄加入直達按鈕、左右手各自重設，跟隨動作時停用不生效的乙與混合控制。沿用原遊戲 oo_hand 的 ShapeHandInfo，不新增硬編手勢清單或使用全身動畫假裝單手姿勢。新增實際 UI 驗收逐一選取左右手所有資源項目、檢查對側骨骼不變，以及動畫／服裝重載保留設定。首個 rg 搜尋再度使用 PowerShell 不展開的檔案位置萬用字元，被拒絕；後續使用明確路徑與目錄 -g。

## E45 靜止 portable 角色不能用動畫採樣刷新手勢

HandsR1 實際 UI 驗收發現，未播放動畫的 portable 角色選擇全部左手手勢後骨骼仍未變化。原因是靜止模式呼叫 ApplyAnimFrame，而 portable session 沒有 game_root 時該函式直接返回。改為靜止模式使用既有 StaticInteractionFrame，從 shaped baseline 重算手勢及後續修飾；播放模式直接由 ApplyPoseFrame 的對應原生／Unreal Animator 流程重算一次，避免多做動畫採樣。單次更新與批次更新皆採同一規則，保留動畫時鐘、物理歷史與對側設定。保留失敗報告，必須重新通過靜止姿勢、全部左右手項目、混合、單側重設、動畫切換與換裝測試，不能只靠原遊戲動畫測試宣稱成功。

## E46 手勢重設按鈕不能只驗證事件而忽略欄寬

HandsR2 八項功能測試通過，但截圖顯示右側窄欄並排的兩個「跟隨動作」按鈕文字互相重疊。改為各自佔完整欄寬並允許換行，沿用既有動作欄捲動，避免硬縮中文字級。最後截圖仍須人工檢視。

## E47 服裝快速切換必須避免重建角色及全域狀態競爭

原先每次半脫／全脫皆重開 Session、建立 GPU 模型、重建動畫選單與語音，磁碟預備檔不能消除整個顯示重建成本。本輪加入最多三個非作用中預覽的 LRU、背景預備穿著／半脫／全脫，依卡片與資源時間戳、服裝索引及完整九槽狀態識別，切換保留現行編輯值與動畫時鐘。背景開啟新增明確、可巢狀且 thread-local 的 requested clothes scope；不可再以 process-global OK_CLOTHES_STATES 讓並行載入相互污染。仍保留原作狀態連動、預製網格及遮罩判定，沒有以隱藏上衣代替半脫幾何。

ClothesFastBuildR1 發現本引擎 SpawnActor 的模板多載不接受單一 FTransform，改用位置與旋轉多載。該輪亦遇到正在修改中的街道程式 Role 隱藏 AActor 成員錯誤；檢查時該程式已改成 Participant，沒有覆寫其他工作。調查初期又遇到 rg 的位置萬用字元及猜測錯誤的 C header 路徑；後續使用目錄 -g 與 rg --files 定位。

ClothesFastBuildR2 的驗收程式以 auto* 迭代 TObjectPtr 容器而無法推導，改為明確的 UOKShapeSlider*。測試碼也必須先完成正式建置，不能以未編譯的驗收程式作為功能證據。

## E48 快取命中仍需驗證材質與精確骨骼目標

ClothesFastR3 首次命中耗時 4,115ms，主要在 PreparePlaybackMaterials 對空資源列表呼叫 FinishCompilation，等待無關全域工作。空清單不再等待；閒置預覽在背景逐影格使用非阻塞準備，僅材質就緒者可快速切換。驗收原先以名稱前綴把手指輔助骨算入，改成精確的 30 個 ShapeHandInfo 目標。補上全脫後半脫的第四種常見狀態：另外五槽維持全脫，不能硬改成穿著。快取切換複製一般語音目錄及音高，避免已準備的背景角色缺少語音目錄。首次補丁因多餘的空 hunk 原子拒絕，重新套用有效補丁後才建置。

## E49 明確服裝請求也必須進入 SQL session 快取鍵

檢查首個未預備狀態時發現，SQL env_sig 仍只讀全域 OK_CLOTHES_STATES，可能把穿著快取用在明確指定的半脫／全脫。新增 explicit requested scope 查詢，SQL 身份使用相同九槽明確值；沒有明確請求的既有呼叫維持原本環境鍵。新增巢狀還原與不同衣物狀態鍵的原生測試。主動停止僅屬於 ClothesFastR4 的驗收程序並保存 aborted.json，不把中斷結果當作成功；重新建置後再完整驗收。

## E50 快速換卡也要限制背景工作數與區分資源模式

檢查失效流程發現重設快取時若立刻清除 in-flight，舊原生載入仍在工作，快速換卡可能堆積多個背景工作。現在維持實際工作閘門到舊工作完成，舊代結果只釋放、不發布，整個 widget 最多一個背景準備工作。快取身份加上獨立資源／原遊戲根目錄模式，避免同一路徑在不同模式共用不同來源模型；快取命中統計由真正重用路徑設定，不把載入失敗誤算命中。首次補丁因同一檔案出現兩個操作區段被原子拒絕，合併成單一檔案區段後成功套用。

## E51 首次切換修改身形的預覽不能重跑完整物理暖機

ClothesFastR5 已通過 32 次原作幾何、遮罩、身形與骨骼、左右手勢、動畫時間與暫停保留，但首次三個切換分別需 330–395ms，超過 200ms 目標；後續為 30–52ms。原因是先套新參數再啟用動畫，觸發 SetPoseMode 的完整 ApplyShape 與物理暖機。現在先選動畫，再以 MarkShapeDirty／ApplyPoseFrame 的即時路徑套用新值；既有待處理身形也先以即時路徑處理，靜止且模式未變時不重跑 SetPoseMode。保留 R5 失敗資料，不放寬門檻，修正後重新量測包含首次切換。

## E52：外觀與模組盤點工具的環境假設（2026-10-08）
Windows rg 路徑不得使用未展開的 card*／build*，應以目錄加 -g 或明確檔名。先用 rg --files 確認 KKCardReader.h 與 build-release.cmd 真實位置，再讀取。Python 本機沒有 tzdata，盤點時間改用使用者 Asia/Taipei 的 UTC+08 固定偏移，不為單一時間戳安裝套件。首次失敗不算盤點輸出成功，修正後重新執行。


## E53：通用外觀屬性編輯的格式與執行順序（2026-10-08）
R1 曾因工作目錄錯誤未執行匯出共用函式重構便啟動編譯，保留失敗 log，改為先完成明確路徑的來源修改再測試。R2 發現 MessagePack 解碼成功可能只解出長度前綴整數，不能據此判定為直接 map；改用 map 型別判斷，否則讀取長度封裝區段。R3 真卡往返通過。飾品 addMove 為 [2,3,flat]，編輯標籤必須轉成兩組位置／旋轉／縮放，不把矩陣維度當參數。切換角色／服裝後不得把舊編輯欄位直接套到新卡，加入來源與服裝身份檢查。匯出服裝狀態改用 thread-local 明確請求，避免重新引入全域環境變數競爭。


## E54：屬性編輯建置與模組容器 API（2026-10-08）
Rust rmpv::Value 沒有 as_map_mut／as_array_mut，R4 編譯失敗後改為 Value::Map／Array 模式比對；R5 外觀測試 11 項通過，包含額外飾品 XML 的其他服裝與未知屬性保留。UHT 禁止派生 USpinBox 的 UFUNCTION 參數 Value 遮蔽基底欄位，BuildR1 失敗後改為 NewValue。失敗 log 保留，不列為成功。


E54 補充：BuildR2 的 TObjectPtr 不提供 IsValid()，改用 IsValid(Get())；MakeShared 回傳 TSharedRef，不能直接套 StaticCastSharedPtr，改以布林／數值分支交給基底型別參數做正常轉換。必須確認實際編譯結果，不以來源已接上代替可執行證據。


## E55：撤銷時舊控制項的失焦事件（2026-10-08）
EditorPropertiesR5 實際 UI 驗收失敗 Pending undo failed，沒有把退出碼 0 當作通過。重建屬性欄位會讓已移除文字框提交失焦事件，重新加入剛撤銷的設定。新增屬性控制項世代，先遞增世代再清除舊列，輸入回呼只接受目前世代。此規則也涵蓋搜尋後的舊欄位。BuildR3 的巢狀 P 變數遮蔽警告視為編譯錯誤，已改為明確 ParentOption；R4、R5 成功建置，但 R5 功能驗收失敗證據保留。


## E56：舊獨立卡模組欄位與顏色分類（2026-10-08）
只解析舊版獨立卡會漏掉被省略的 KKEx，C000 初次只有 1573 個原作欄位。屬性讀取與套用統一先做既有精確來源恢復，原生 R7 確認恢復後 2258 欄位，其中 685 個模組欄位。MaterialEditor 的 Original 還原用欄位保持唯讀，不當作一般參數。colorInfo 是衣物顏色設定容器，裡面的花紋 tiling／offset／rotate 不是 RGBA；分類必須只讓實際 Color 值繼承色彩範圍，新增 tiling 應為 number 的真卡檢查。所有待提交修改與輸出資源完成前，不能宣稱逐功能驗收已通過。

## E57：另存驗收必須涵蓋產品的資源保存流程（2026-10-08）
EditorPropertiesR8 的 11 項及 HandsR8 八項通過，但 ClothesR8 在九個動畫通過後因未準備服裝規則失敗。原因是驗收只用 UKKCardWriter::SaveWithEdits 寫 PNG，沒有像產品 OnSave 一樣接 FinishPortableSave／CopyPortableClothingStates；低階寫卡 API 不負責保存服裝回執。改為驗收沿用產品保存步驟並等待 bPortableSaveInFlight 結束，再重新驗證另存卡。保留失敗報告，不把它說成產品保存 bug，也不只手工補測試卡的 sidecar 後宣稱通過。


## E58：額外飾品不能沿用固定二十欄目錄（2026-10-08）
MoreAccessories 把第 21 欄起存為每套服裝的 XML 順序，原有固定 20 欄圖片選單無法選擇這些素材。改從目前卡片的完整 KKEx 與服裝座標建立動態目錄，圖片套用維持同一槽號的 resolver property，並初始化原作要求的兩組變形與四層顏色屬性。既有未知 XML 屬性及其他座標必須保留；新增空欄不重新編號既有欄位。開啟目錄不能因已有快取而跳過卡片／座標重讀，非同步結果必須驗證目前選取與載入世代。此項須等原生與實際模型驗收，不能以 XML 測試取代顯示結果。
查找建置入口曾誤讀不存在的 Tools/ok-extract/build.ps1；正確入口為 build-release.cmd，後續先用 rg --files 確認路徑。
E58 補充：查閱字體套用位置時再次臆測不存在的 OKGalleryEditorLayout.cpp；已改用目錄配合 -g 搜尋符號。查找符號時直接搜尋已確認的來源目錄，不再先猜檔名。

## E59：新增欄位後的目錄必須自動更新（2026-10-08）
R10 六份實際報告合計 64 項通過，但流程檢查發現驗收主動重開圖片選單，掩蓋新增欄位後仍顯示舊目錄的使用體驗。將目錄讀取抽為共用函式，新增欄位成功後在角色載入已啟動的世代讀取新卡片目錄，自動選中最後新增的欄位；對外開啟入口仍保留載入／忙碌檢查。驗收移除手動重開步驟，直接要求新欄位與選取立即出現在目前選單。必須重新建置並驗證，不能只將「重開選單」寫進操作說明。

## E60：驗收不得把假髮改綁頸部後當正常髮型展示（2026-10-08）
Properties R9–R11 為測試 parentKey，刻意將第 1 飾品欄的假髮綁到 a_n_neck；ExtraAccessories R10–R11 又沿用這張測試輸出，造成截圖禿頭、假髮位於胸前。改用第 2 欄普通髮夾測試變形與換骨架，原假髮綁定必須保持。舊報告僅證明資料修改流程，不能作為正常髮型或效能合格證據。新增獨立效能量測，使用原始正常卡；驗收標準為切換完成不超過 100 ms，完成後至少 60 FPS。查 log 再次誤用 PowerShell 不會展開的路徑萬用字元，改用已存在目錄與 -g 篩選。

## E61：分頁控制項的驗收需沿用真正搜尋事件（2026-10-08）
詳細參數已改每頁最多 64 項，PropertiesHairR12 驗收的程式只呼叫 SetText，沒有觸發 OnTextChanged，故仍在舊頁搜尋髮色控制項並失敗。改為測試明確呼叫產品 OnPropertySearch，不能把資料存在誤當控制項已顯示。保留失敗報告。效能 DetailsR2 為改善前的觀測報告，雖整體讀取成功，最低 54.23 FPS 未達門檻；後續報告必須以效能條件決定是否通過。Windows Python 讀取原始碼需明確指定 UTF-8，不能依賴 cp950 預設編碼。

E61 補充：查到 RefreshPairs 位於 OKGalleryWidget.cpp 後，又錯讀不存在的 OKGalleryPair.cpp。後續必須直接使用 rg 回傳的實際位置，不以概念推測檔名。

## E62：增量資源保存需驗證資料型別與原始基準（2026-10-08）
NativeR15 的測試直接比較 AccMove 陣列，但來源型別沒有 PartialEq，編譯失敗；改為逐一比較位置、旋轉、縮放，不為測試修改產品型別。其他座標比較必須使用修改前的完整原卡，不能重新解析修改後資料當基準。R16 執行時又把根目錄相對路徑交給 Rust 子目錄工作目錄，修改未執行；後續 Python 檔案修改一律使用絕對路徑。保留失敗 log，重新跑原生回歸。

## E63：頭髮位置驗收需分辨綁定與合法形變（2026-10-08）
PropertiesHairR13 比較整束假髮包圍盒而失敗。Native WigBoundsR1/R2 確認原始頂點與三個來源綁定合約雜湊完全相同；頭頂高度相同，頭頂綁點平移只差約 1 mm，但舊獨立卡未帶出的 31 個 ABMX 設定恢復後，頭部 Z 縮放從 0.8017 恢復為 0.9620，髮尾包圍盒因此合法變化。不能放寬整束頭髮的容許位移來掩蓋結果；改檢查不可變模型／骨架／綁定合約、假髮 parentKey 和頭頂高度，並檢視真正顯示的截圖。DiagnosticR1 截圖正常，平均 285.86 FPS，但最低 55.38 FPS，嚴格效能結果維持失敗，不以平均值替代。

## E64：功能回歸與效能採樣不能同時跑重工作（2026-10-08）
ExtraAccessoriesSpeedR12 的七項功能成功，且回執確認只重建一套、沿用六套；實際點選到顯示仍需 50117.32 ms，明確未達 100 ms。完成後平均 200.99 FPS、最低 22.54 FPS，但取樣尾段與 NativeR18 的編譯／原生測試重疊，這份 FPS 不能當作隔離測試結果。後續效能測試只跑一個擁有的 UE 行程，測量期間不啟動建置或原生測試；保留失敗報告，不刪除離群幀或只報平均。


## E65：FPS 採樣時機及已知工具路徑錯誤

- 舊飾品驗收在保存並等待衣物預載結束後才量 FPS，漏掉「切換完成後」仍進行的背景工作；不能用此數據證明使用者要求。新版改在可見切換完成的下一幀起連續採樣 300 幀，納入背景預載，不執行其他編輯指令，保存逐幀時間。功能保存驗證在採樣後進行。
- 本輪再次對 Windows `rg` 傳入萬用字元路徑，以及誤用不存在的 output/CrowdAPNG/C000.png。改以實際目錄配合 `-g`；驗收卡片從前次 launch.json 取得 Content/CharacterCards/CrowdAPNG/C000.png。失敗命令未啟動 UE、未更動卡片。


## E66：編輯器建置的引擎相依與檔名查找

R8 被 -NoEngineChanges 擋下：UBT 提示重建既有 UE NetCore 與引擎模組清單。保留失敗日誌，不移除安全旗標或改寫引擎，改以 -UsePrecompiled 使用現有引擎二進位建置專案（R9）。本輪亦猜測了不存在的 OkSession.cpp；實際封裝在 KKUnityBundle/Private/KKRustExtract.cpp，應先用 rg -l 搜尋類別定義再讀取檔案。


## E67：Unity build 的驗收註冊變數撞名

R9 使用既有引擎成功開始編譯，但 Unity 合併編譯單元將 OKEditorPerformanceAudit.cpp 與 OKPaintPerformanceAudit.cpp 一起編譯，兩者都宣告全域 static PerformanceAudit，造成 C2374/C2086。只修正本工作新增的編輯器變數為 EditorPerformanceAuditCommand，未改動 Paint 程式或停用 Unity；不能依靠先前 adaptive non-unity 建置成功來判斷所有組態可建置。


## E68：穩態 FPS 漏算背景模型建立卡頓



## 嚴格實測 R14（R11 DLL）

七項飾品功能成功，但整體 `valid=false`、退出碼 1，兩項效能門檻都失敗：

- 點選至可見完成：37731.82 ms（要求 ≤100 ms）。
- 完成後立即採樣至背景預載結束：24,007 幀，平均 235.33 FPS、最低 2.11 FPS。
- 最長幀 474.41 ms，30 幀超過 16.667 ms。背景建立隱藏衣物角色會造成稍後卡頓，不能以平均 FPS 宣稱達標。
- 報告：Saved/Tests/EditorAcceptance/EditorExtraAccessoriesSpeedR14/report.json；保留全部逐幀數據、啟動／結束 DLL 雜湊核對及正常頭髮截圖。

EditorPerformanceUnitR12 十項全部 Success／退出碼 0，涵蓋重置、中文、衣物、模組／座標、左右手、相機、PNG 快取、貼圖 mip 及唯讀 GPU 資源重用。本輪 18 項原生、13 項 GUI 回歸、10 項 UE 單元與 7 項新增飾品功能成功；效能要求未完成，不標為完整驗收通過。

## E69：原生測試的工作目錄與零案例不可當成功

新增單欄快照測試時，命令工作目錄已是 Tools/ok-extract，Python 卻再次使用 Tools/ok-extract 前綴，導致找不到檔案；後續 cargo 過濾器可能執行零案例。修正為絕對路徑，逐一檢查編輯命令結果與實際案例數，不能只看 cargo 退出碼。此錯誤已發生過，後續 Python 編輯一律由 workspace 的絕對根目錄解析。

## E70：分批不能改變材質上下文；耗時必須由引擎實作確認

分批 renderer 建立必須保留原同步迴圈的材質上下文，已改成同一載入工作的持續狀態，避免每幀重新初始化導致材質不同。查閱 UE5.8 TextureRenderTarget2D.cpp 確認 UpdateResourceImmediate 本身是排入 render command，名稱不足以判斷有同步等待，不能據名稱任意刪掉清除步驟。單欄資源測試首次逐位元比對失敗，仍在追查；不視為通過。大型 bincode assert_eq 會輸出整個資產，改為只列出差異欄位。

## E71：新增較早的型別使用需要提前宣告

非同步預覽欄位放在 OKGalleryWidget.h 原先 TObjectPtr<class AKKPreviewActor> 之前，不能依靠後面的宣告，R13 編譯因此失敗。已在類別外提前宣告 AKKPreviewActor，修復第一個根因後重新建置，不逐一修補後續連鎖錯誤。

## E72：HashMap 序列化順序不能當材質內容差異

PartPatchNativeR2／R3 的逐位元 PartBlob 比對失敗，定位 cf_O_face 的逐欄值後差異為空；MeshMaterial 的 colors／floats／tex_wraps 使用 HashMap，反序列化後迭代順序不固定，bincode bytes 不適合當這些映射的內容相等判斷。改為完整序列化欄位值比較（映射依鍵比較、向量順序與所有幾何／貼圖數值仍檢查），未變更生產格式。全域值以同一資源合併讀回流程為基準，保留原有座標正規化規則。其他套裝的原壓縮快照依然要求逐位元保留。

## E73：增量輸入的全域參照也必須驗證

PartPatchNativeR4 已通過全部 renderer 與全域內容比對，但毀損輸入測試發現 delta 的重複全域參照因保留 base 全域而被略過。不能以最後結果有效替代輸入有效，已先驗證 base／delta 的全部參照索引、重複目標與殘留資產，再進行移除／重排；修正需要重新跑測試。此 helper 仍未接入使用者快速切換路徑。

## E74：保持姿勢不能重設同一個姿勢

檢查 geometry-only 完成流程時發現 SetPoseMode(0) 會重新 ApplyShape／RefreshSkinnedDisplay，即使原姿勢也是 0 仍會重建顯示，抵銷分批建立。改為只有姿勢模式確實改變才呼叫，並在恢復動畫後帶回其浮點參數與左右手設定。PortableSourcePath 實作位於 Plugins/KKCard，須先在 Source／Plugins 搜尋宣告，不能再猜測 KKUnityBundle 中有同名檔案。

## E75：非阻塞編譯也會同步提交過多工作

R15 的最大 244.54 ms 幀發生於預覽完成後一次提交多個材質編譯工作，非 Wait 並不代表整個呼叫沒有同步成本。PreparePlaybackMaterials(false) 改為每次最多提交一個材質，其餘保持未就緒並在後續 tick 繼續；Wait=true 的明確載入邊界保留完整提交／等待語義。純原生 ApplyBodyPhysics／ApplyEditorMods 也改由 Session 的獨占背景工作執行，交付時只補套已變更的設定。此修正待新建置與嚴格實測，不宣稱已達標。

## E76：網格完成不能代替材質就緒

分批後的採樣必須涵蓋最後一個隱藏角色的材質提交／編譯，原先只看原生工作與網格佇列空會漏掉這一段。已補上所有快取角色的 bPlaybackMaterialsReady 條件；主角色也等待非阻塞材質準備成功才顯示並清除忙碌狀態。非阻塞提交改以遊戲全域幀號限制每幀最多一筆，防止多個 actor 同一幀各提交一筆累積超出預算。等待模式仍可在明確載入邊界完整提交。

## E77：預載排程本身仍讀取完整規則

R18 日誌從可見完成到背景服裝工作開始仍相隔約 1.24 秒，定位 WarmClothingPreviews 連續四次同步讀取／驗證服裝規則。已改為排程旗標，規則準備與原生模型共用一個背景工作閘門，結果按世代與資源 context 驗證後才加入佇列；重設會撤銷未執行排程。嚴格採樣也檢查規則排程旗標，不能在尚未發動時誤判預載已結束。新變更待重新建置驗收。

### E70／E73／E75–E77 驗證更新：R19

E70／E73 的修正已由 EditorPartPatchNativeR5 一項實卡測試通過，涵蓋全部欄位語義、全域保留與毀損參照拒絕；原始 bincode 映射順序差異不再誤判。E75–E77 已成功建置並通過最新 13 項外觀 GUI、8 項額外飾品功能、11 項 UE 單元回歸。

效能仍失敗：EditorExtraAccessoriesAsyncR19 點選至可見完成 42578.0167 ms，最低 8.6778 FPS，83 幀超過 16.667 ms。必須避免重複錯誤：背景載入不是總工作量減少；分批一個 renderer 也不保證單批小於幀預算；材質準備完成前不能解除忙碌，規則與材質預載完成前不能結束採樣。下一步須真正單欄擷取／渲染資源重用；不可用延後採樣、平均 FPS 或全域實驗性 GC 設定掩蓋失敗。所有報告保留，未標為完整驗收通過。

## E78：跨 Session 網格指紋未命中需查清輸入差異

EditorMeshReuseR20：原有 11 項 UE 單元通過，新增 SkeletalMeshCache 測試失敗於獨立 Session 重用，同一 Session、幾何／UV／權重／骨架／morph 失效及弱參照回收檢查未報錯。不得移除骨架或 morph 內容以強迫命中；正在逐欄比較真實建置輸入，查明是否為來源變動或快取指紋錯誤。失敗報告保留，尚未宣稱快取驗收成功。

R21 診斷確認差異來自原生 skel_export 的 HashMap 子樹遍歷：同一張卡的骨架順序及 bone_idx 不同。改成匯出專用的固定父先順序，兄弟節點依原作 sibling_index、缺少資料時依名稱／來源 id 穩定排序；根節點同大小時也固定選擇。不改全域 pose 遍歷與動畫操作，不略過任何骨架／蒙皮輸入。新增插入順序排列及實卡四次快取讀回完整 GPU 契約測試，待新建置驗收。

## E79：舊動畫測試不能用環境變數修改已建立的 Session

EditorExportRegressionR22 四項通過（包含真實身體／臉／服裝蒙皮重建及父先骨架），一項 different_clips_change_leg_bones 失敗。該舊測試只修改 OK_POSE 再套形變，但 Session 現在於建立時讀取預設動作，後續使用自己的 anim_clip；三次實際仍是相同動作。改為明確設定該 Session 的 anim_clip，保留原本不同動作必須改變腿骨的斷言，且避免測試修改全程序環境影響其他案例。失敗報告保留，修正待重跑。

### E78／E79 修正驗證

EditorMeshReuseR22 的 12 項 UE 單元全部 Success／退出碼 0：新增網格快取已證明獨立 Session 與骨架物件能重用相同資產，幾何／UV／權重／參考骨架／morph／描邊改變會失效，顏色保持獨立材質，未引用資產能由 GC 回收。原有 11 項回歸保持通過。

EditorExportRegressionR23 五項原生測試通過、零失敗、四項明列 ignored，沒有將 ignored 計入通過數。不同動作、父先唯一骨架、固定樹順序、姿勢來源索引及真實身體／臉／服裝蒙皮重建均通過。另有 EditorExportDeterminismR22 一項實卡快取完整契約通過。最新 GUI 與嚴格效能待 R23 建置驗收，未宣稱 100 ms／60 FPS 已達成。


## E80：有容量上限但沒有淘汰會造成持續快取失敗

R23 反覆載入耗時由約 0.5 秒升至約 40 秒。原作 SQLite 快取已接近 4 GiB，零閒置頁面；寫入程式忽略 INSERT 錯誤且沒有回收機制。容量上限只能防止增長，不能保證新結果能持久保存。不得刪除整庫或取消預算掩蓋問題；新增有界淘汰並以真 SQLite 滿容量測試驗證。實卡耗時是否由此改善仍待重新量測，不先歸因為已證實。


### E80 回歸驗證 R25

真 SQLite 1 MiB 上限填滿至 SQLITE_FULL 後原地升級，能淘汰舊 session／prefab 並連續寫入新資料；熱門資料與來源索引保留，過大單筆不清空工作集，非容量寫入錯誤不觸發重試淘汰。跨程序讀取後資料被刪除時不建立孤立使用紀錄。EditorCacheRegressionR25：11 passed、0 failed、2 ignored；仍待新 UE 建置與實卡整合。

## E81：嚴格 FPS 採樣不能與編譯重疊

EditorExtraAccessoriesMeshReuseR23 功能 8 項通過，切換 51079.8793 ms；18225 幀平均 138.2533 FPS、最低 14.9717 FPS、最長 66.7927 ms、768 幀超出預算，整體失敗。該輪採樣期間另行編譯 Rust 快取測試，競爭 CPU／I/O，不能據此判斷網格重用的幀率改善或退化。保留原始報告及失敗，不刪除長幀。後續嚴格驗收須等所有本次編譯及其他測試完成再啟動；單元與功能可並行但不得與效能採樣並行。


## E92：來源單欄更新不代表完成可見切換效能

EditorSingleSlotExtraIsolatedR27 確認正式 GUI 單欄擷取、保存、頭髮位置等 8 項功能通過，但點選至可見 4906.1713 ms，30832 幀最低 13.6358 FPS、最長 73.3365 ms、15 幀超預算，整體失敗。平均 371.6902 FPS 不能當成持續 60 FPS。不能把原生擷取／保存的完成當成只替換 UE 單一 renderer；目前 UI 還會重建整套顯示並預備衣物。需實作保留未變動顯示、索引更新、非同步保存交易與必要資源預備，保留所有背景採樣後重新驗收。

### R27 本輪驗收補充

原生完整契約、正式卡片更新、來源索引 patch 三項分開精確驗證均通過；快取 11 項、服裝容器 3 項通過。新版建置成功，12 項 UE／13 項外觀 GUI 通過。E91 衣物在其他 UE 程序退出後重新執行 EditorSingleSlotClothesIsolatedR27：7 項通過、32 次切換最長 165.0008 ms，保留先前失敗，不推定外部工作是唯一原因。飾品新／舊卡各 8 項功能通過，但嚴格效能均失敗，數值詳 CHARACTER_EDITOR_SINGLE_SLOT_R27.md；單欄來源更新不代表 UI 只建立單欄元件。

## E91：衣物功能正確與切換時間通過須分開記錄

EditorSingleSlotClothesR27 完成 32 次快取切換，5 項功能（狀態預備、顯示替換、身體／骨架形變、左右手／時鐘／暫停、遮罩）通過，但第一次半脫 201.1597 ms 超過既有 200 ms 門檻，整輪失敗，後續兩案例未執行。其後最大 176.4841 ms、暖切換約 31–48 ms，不得刪掉首筆或提高門檻算通過。當時另有 E:/PhysX 的 UE GPU 探測程序，不能僅由這次小幅差異判定回歸原因；保留全部數字並在環境可隔離後重測。

## E90：Cargo 篩選是子字串比對，實卡測試應使用完整名稱與 --exact

EditorSingleSlotRegressionR27 的 single_ 篩選除了兩項新測試，也匹配既有 expression_sparse_snapshot_and_single_publish_match_legacy；該測試缺少 OK_TEST_LEGACY_PORTABLE 而失敗。兩項新測試通過，但整輪不能算通過。保留報告，改用完整測試名稱及 --exact 分開驗證，避免將不相關的 ignored 測試在缺少 fixture 時一起啟用。

## E89：有效預設狀態相同時不應使擷取依賴失效

R27 正式 export_edited 路徑明確請求全穿，而來源擷取的未指定衣物狀態也代表全穿。依賴卻使用環境字串空值與九個 0 直接比較，導致不必要的完整載入。Context 指紋改以有效 requested_clothes_states 正規化，其他衣物狀態仍保留差異；提升來源快取管線版本，新增預設／明確全穿等價檢查。不得以放寬骨架、形變或來源貼圖的依賴檢查取代正規化。

## E88：整合驗收須覆蓋正式發布卡片的最後步驟

R27 第三次比較發現假髮幾何不同，但原始資源參照全部相同。原因是測試直接讀取 shared_edited_accessory 的精簡中間卡，跳過 export_edited 還原完整 KKEx（包含 ABMX）的正式步驟。改從正式 export_edited 路徑新增／重置飾品，檢查 receipt 證明確實走單欄，並比較最後可保存卡片。不可為通過中間產物測試而改掉既有完整模組 metadata 保存規則。

## E87：顯示不變驗證要使用相同載入模式比較

R27 單欄 APNG 更新已成功走正式入口，所有原始資源參照與其餘服裝快照一致，但測試拿來源擷取幾何與 APNG 展示幾何直接比較。展示載入原本會還原可編輯形變而不再次套用 PushUp，兩者不應當作同一基線。改為更新前後都使用 portable::load 比較，同時保留來源擷取契約及全部原始參照逐項檢查；幾何失敗只輸出部件名稱，避免龐大陣列掩蓋診斷。

## E86：單欄來源擷取須與完整擷取使用相同解析模式

R27 APNG 整合首次未走單欄入口：完整來源擷取會套用原卡形變模組，單欄入口卻使用展示用 parse_card_bytes_mode(false)，造成有效依賴不一致。不能移除依賴檢查；改用相同來源解析模式 parse_card_bytes，保留展示模式的既有規則。額外輸出明確恢復原因，重新驗證實卡新增、移除及其他服裝快照不變。

## E85：位移驗證不能假定 HideHair 飾品可旋轉

R27 第二次實卡測試三個欄位均精確一致，但假髮位移斷言失敗。原作 HideHair=1 規則會將兩層 N_move 歸零，旋轉沒有影響才正確。保留生產行為，改用原作項鍊 category124/id14 檢查位移，並比較全部部件契約，不只猜測 positions 或單一 runtime 欄位。

## E84：空飾品欄位不能只認定類別 120

R27 實卡驗證假髮三個 renderer 已精確一致，但額外空欄位儲存 type=0，單欄入口誤當無效類別。修正沿用完整載入的 is_mesh_category 篩選，非網格類別視為沒有 renderer；不猜欄位名稱。另實卡 slot 1 本來也是空欄，位移測試改使用確定有內容的 slot 0，避免假定一般欄位必定有飾品。保留初次失敗報告並重新驗證。

## E83：新增測試前應先查實際欄位與語法

R27 單欄契約測試初次編譯使用不存在的 AccessoryRuntime.local_moves，且 Rust 小數寫成 .01。查閱結構後改為 local_placement 與 0.01。後續新增測試先核對實際型別，以 cargo check --tests 通過後才執行昂貴實卡驗證；編譯失敗不能算驗證成功。

## E82：停用 Actor Tick 不等於停用骨骼網格元件 Tick

衣物預覽池隱藏角色並停用 Actor Tick，但原先骨骼顯示元件保持 AlwaysTickPoseAndRefreshBones，仍可能每幀更新。修正應保留各元件原始旗標，暫停隱藏顯示，分批建立後補納入新元件，取出快取時還原。不能全部強制打開，避免破壞既有協調播放／本來停用的元件。新行為待實卡回歸與切換驗收，不能僅依程式檢查先宣稱 FPS 改善。


### E82 驗證更新 R26

新建置成功。EditorInactivePreviewR26 六項 UE 回歸通過，實卡驗證所有原始旗標、晚加入 renderer 與 Actor 播放狀態不變。EditorClothingInactivePreviewR26 七項 GUI 通過，32 次切換驗證形變、骨架調整、左右手、動畫時鐘、幾何及遮罩正確；最大 190.9821 ms，未冒稱小於 100 ms。嚴格 FPS 待隔離報告。


### E80–E82 最終驗收狀態 R26

快取 11 項原生回歸、R25 12 項 UE／13 項外觀 GUI、R26 6 項 UE／7 項衣物 GUI／8 項飾品功能均通過；最新頭髮截圖已檢視。R26 飾品嚴格效能與本次其他編譯／測試完全分開執行，修正 E81 的驗收干擾。結果：點選至可見 36880.3430 ms，20029 幀最低 16.3567 FPS、平均 200.8508 FPS、最長 61.1369 ms、超預算 39 幀。整體失敗已保留，不能把平均 FPS、非同步或元件暫停視為達到要求。真正單欄擷取／替換仍未接入，相關來源貼圖池／骨架／索引契約不得省略或近似。

### E93 — Windows 搜尋目標不得使用目錄萬用字元或猜測標頭檔名（R28）

本輪讀取曾將 `Plugins/KKCard*` 交給 rg，並猜測不存在的 `KKCharacterFile.h`，造成路徑錯誤。改用 `rg --files Plugins/KKCard/Source/KKCard/Public` 取得實際清單；型別在 `KKCardTypes.h`。之後搜尋使用已存在的目錄配合 `-g`，不重複猜測路徑。無程式或資料變更受影響。

### E94 — 以工具子目錄執行命令時，錯誤紀錄必須使用絕對路徑（R28）

原生測試工作目錄為 Tools/ok-extract，同一命令前段以相對路徑 Docs/CHARACTER_EDITOR_ERRORS.md 寫紀錄失敗；測試已啟動但紀錄未寫入。立即改用工作區絕對路徑補記 E93 及本項。未建立另一份錯誤紀錄，也不因前段失敗重啟已運行的測試。

### E95 — 不存在的輸出父目錄不一定造成 UE 保存失敗（R28）

EditorAsyncCardIntegrationR28 的新交易測試假設不存在的父目錄會使 SaveWithEdits 失敗，但 UE 檔案層自動建立目錄，導致請求仍被呼叫而斷言失敗。既有 12 項回歸通過，新測試失敗原始紀錄保留。改用已存在的目錄本身作為檔案輸出目標，以確定觸發寫檔失敗；不修改正式錯誤處理或放寬斷言。修正後重新建置並用新標籤驗證。

### E96 — 詳細設定幾何驗收必須使用含實際第 2 欄飾品的 fixture（R28）

EditorAsyncCardPropertiesR28 誤用單欄回歸生成的 reset.png；該卡已清空 accessory1，而 Properties audit 的旋轉／縮放／綁定固定修改 accessory1。前 7 項控制項驗收成功，之後因空欄沒有可變動幾何而失敗，無 report.png。不得將空欄修改當成功，也不得在未產生截圖時呼叫 view_image；本輪曾嘗試讀不存在截圖而獲得檔案錯誤。保留報告，以原始 C000 卡和新標籤重新驗收；正式產品與斷言不變。

### E97 — 背景保存／讀回成功不代表切換與逐幀效能通過（R28）

EditorAsyncCardExtraR28 的 8 項功能成功，背景輸入保存 245.3815 ms、輸出讀回 175.8237 ms，worker_thread=true；source receipt 確認 single_slot=20、重建 1 renderer、保留 34 renderer 及 6 套其他服裝。但點選至可見 7868.7986 ms、37,890 幀最低 7.22646 FPS、最長 138.3804 ms、18 幀超過 16.667 ms，valid=false／退出碼 1。保留全部長幀與失敗報告。啟動、採樣中、結束皆有另一個 E:/PhysX UE 工作，不能作隔離效能改善比較，也不能直接歸因外部負載。原生 preview_build=3844.6628 ms，UE 仍完整重建顯示及背景衣物來源；必須繼續做索引安全的顯示部件重用、預備資源及可見更新／最終保存分離，不能以平均 310.72 FPS 宣稱達標。

### E98 — 監看進行中工作時必須先確認紀錄存在，工具子目錄讀取一律使用絕對路徑（R29）

第一次素材目錄測試尚在編譯時讀取未產生的 EditorCatalogContractR29.log，得到檔案不存在；應先列出已建立的紀錄或 Test-Path，不因尚無檔案判斷測試失敗。稍後再次在 Tools/ok-extract 工作目錄使用工作區相對路徑讀 list.rs，重犯 E94 的路徑假設；已以絕對路徑重新讀取，未重啟已在執行的測試。後續此類混合工作目錄命令全部用 C:/portable/OpenKoikatsu/... 路徑。

### E99 — 目錄查詢快取命中不能代表整個可見切換及逐幀效能（R29）

1,851 筆原作清單暖查詢 0.085 ms，完整來源契約、正式卡片更新及 13 項 UE 回歸成功。但 EditorCatalogExtraR29 仍為切換 5281.9743 ms、最低 6.19176 FPS、18 個長幀，整體 valid=false；8 項功能成功且假髮正確。啟動／結束無其他 UE，不能再假設外部負載解釋長幀。保留失敗證據，下一步以 UE CPU trace 定位耗時，不以平均 371.70 FPS 或暖查詢宣稱通過。environment-mid.json 寫入時程序已結束，只可視為結束後快照；之後環境檔保存 recorded 時間並明確區分實際採樣期間。

### E100 — 不猜測外掛內檔案位置，先用 rg --files 取得實際路徑（R30）

本輪讀取 KKPreviewActor.cpp 時誤放到 Source/OpenKoikatsu，實際位於 Plugins/KKUnityBundle/Source/KKUnityBundle/Private；另誤讀 Private/KKCardWarmLoad.spec.cpp（實際在 Private/Tests）與 Public/ok_extract.h（實際在 Tools/ok-extract/include）。均為讀取失敗，沒有修改錯誤目標。之後搜尋以已存在目錄及 rg --files 取得檔名清單，不再依名稱猜路徑。CPU trace 仍正常完成，保留全部證據。

### E101 — 跨模組呼叫必須明確匯出 API（R30）

第一次 R30 建置在 OpenKoikatsu 連結時失敗：新呼叫的 OKSkelMeshEnabled 原本只供 KKUnityBundle 內部使用，宣告缺 KKUNITYBUNDLE_API，導致 LNK2019／LNK2001。已在正式公共標頭匯出函式，不使用重複讀環境或另一套 renderer 模式判定。原失敗建置紀錄保留；以新標籤重新建置後才驗收。其餘眼睛 UV 精度與既有 incremental GC 訊息為建置警告，未隱藏。

### E102 — Windows rg 路徑不可含檔名萬用字元（R30 再犯 E93）

搜尋材質程式時再次將 Private/KKMaterial*.cpp 直接當 rg 路徑，Windows 回報錯誤 123。已改為實際存在的 Private 目錄搭配 -g '*.cpp'；不能因為 PowerShell 顯示萬用字元字串就假設原生 rg 會展開。此為查詢失敗，沒有來源修改；後續原生工具的路徑參數只放實際存在的目錄或明確檔案。

### E103 — Patch 的上下文必須來自實際讀取，不加入猜測的無用區塊（R31）

增加單欄階段計時時，同一 apply_patch 夾入與此次修改無關且未核對的測試註解占位，驗證失敗，整份 patch 未套用。已用 rg 確認沒有部分變更，再只套用真正必要且已讀取的上下文，另以實際檔尾新增契約測試。不得把 patch 失敗當成已修改，也不盲目重複提交相同內容。

### E104 — 首次載入前的模式快取不能代表即將套用的 Live 渲染請求（R31）

R30 首輪在主執行緒擷取 OKSkelMeshEnabled，但第一次 Open 尚未呼叫 OKPrepareLiveGpuEnv，可能得到尚未初始化的 false，漏做首次背景骨架預備。後續飾品／衣物已有 worker=1 紀錄，不能據此宣稱首次也涵蓋。修正為共用 OKLiveGpuSkinRequested，與 OKPrepareLiveGpuEnv 使用同一判定；新增未初始化狀態與明確 CPU 模式測試，讀取請求本身不修改全域模式。

### E105 — 原生 helper 新增診斷欄位必須傳到正式驗收 receipt（R31）

EditorPartCardPatchR31 的正式來源新增測試失敗：shared_edited_accessory 已生成細部時序，但 appearance::export_edited 只選取既有欄位寫入 verified.json，新欄位被漏掉。已補上正式轉傳，不移除有限、非負時序斷言；失敗紀錄及產生的 fixture 保留，用新標籤重跑。這次測試在新增時序斷言即停止，不能把未跑到的新增／重置與其他服裝保留當已通過。

### E106 — 已找到 mods.rs 後不再假設存在同名子目錄（R31）

搜尋 apply_strip_all_migrations 已在 mods.rs 找到定義，卻又對不存在的 src/mods 目錄發出第二次搜尋，造成讀取錯誤。已直接讀取實際結果 mods.rs；無資料變更。找到唯一實際來源後應依該結果繼續，避免多餘的路徑猜測。

### E107 — 同長 XML 不保證 ZIP 檔案同大小（R32）

EditorMigrationReuseRegressionR32 為 15 passed、1 failed、3 ignored；失敗是新增 fixture 假設 old/new 同長文字經預設壓縮後檔案長度也相同（實際差 1 byte），尚未到同大小失效斷言。修正測試 ZIP 使用 Stored，以確實產生同大小改寫；不改正式快取、不移除大小斷言，重新跑全部模組回歸。另將通知不可用平台的測試明確要求資料一致，只有 Windows 要求同 Arc 重用，與正式保守讀取策略一致。

### E108 — 來源目錄通知不能保證覆蓋連結目錄的外部目標（R33）

審查 R32 共用遷移快取發現 collect_zipmods 會跟隨目錄連結，而只監看 mods 根目錄可能漏掉外部目標中的變動。R33 的遍歷保留來源讀取，但遇到 Windows reparse／符號連結或無法判定 metadata 時將結果標為不可快取；增加實際 junction 測試，驗證外部 ZIP 同大小改寫不留下舊遷移。這是已識別的有效性缺口，不能僅以普通目錄測試通過而宣稱涵蓋全部來源。

### E109 — 排入渲染命令不等於預備顯示已就緒（R33）

原非阻塞 PreparePlaybackMaterials 在 SendAllEndOfFrameUpdates 後立刻設 bPlaybackMaterialsReady=true，未確認 RHI 資源提交完成。已改以 RHIThread fence 輪詢，完成前保持未就緒，取消時清除待發布狀態；需要同步的既有邊界仍可等待。增加實際 render command 閘門測試，驗證非阻塞返回、完成後發布與動畫相位不變。R32 衣物功能通過、最長 184.9502 ms 仍不能當作 100 ms 或此新修正已驗收。

### E110 — Windows 搜尋路徑不可混入檔名萬用字元（R33）

搜尋 OKGallery*.cpp 時再度將萬用字元直接放入 Windows 路徑，rg 回報 os error 123，未修改資料。已改為搜尋實際 Source/OpenKoikatsu 目錄並以 -g 'OKGallery*.cpp' 篩選；後續一律使用目錄加 glob 篩選，避免重複的無效路徑查詢。

### E111 — 介面驗收日誌必須依實際檔案清單取得（R33）

直接猜測 editor.log 造成讀取不存在檔案；實際腳本輸出為 run.log，已列出目錄後改讀該檔。未影響正在執行的驗收；後續新標籤先查腳本或目錄清單再讀取。

E111 補充：搜尋原生 Session 實作時又猜測不存在的 KKLiveSession.cpp；已直接在實際模組目錄搜尋方法定義，定位 KKRustExtract.cpp。Session 不是獨立同名檔案，後續以符號搜尋取得檔案路徑。

### E112 — 衣物渲染就緒修正不能視為速度達標（R33）

EditorSourcePreparationClothesFastR33 完成 32 次切換，最大 212.9688002 ms，整體 valid=false（既有 200 ms 門檻也未通過）。保留完整 switches 與幾何／遮罩指紋，不將 R32 的 184.9502 ms 或 R33 整合測試通過當作新版切換成功。100 ms 仍未達標，後續分階段初始化必須重新量測。

### E113 — R33 功能正確性不代表完整切換期間效能成功

額外飾品 8 項功能成功，但切換 6446.7418976 ms；25,543 幀最低 3.7552381 FPS、最長 266.2946992 ms、141 幀超預算，整體失敗。啟動及結束無其他 UE，不能把失敗直接歸因外部負載。保留完整報告、來源 receipt、交易時序與截圖；後續只比較各版正式未開追蹤的完整採樣。

### E114 — 拆分初始化階段必須保存跨階段狀態（R34）

EditorIncrementalSetupBuildR34 編譯失敗 C2065：陰影旋轉原本是初始化區塊區域變數，捕捉元件移至後續階段後不可讀取。修正將該次初始化計算的旋轉保存於 FSessionLoadState，右／上基底與捕捉元件共用同一快照；不在後續階段讀取可能已變更的方向重算。原失敗建置保留，重新建置後才執行測試。

### E115 — 初始化拆分只解決部分背景工作，300 ms 仍未達標（R34）

17 項 UE 與衣物正式 7 項功能／32 次切換成功，但飾品 8 項功能成功不代表效能通過：5186.2038001 ms、最低 5.1937502 FPS、最長 192.5391033 ms、38 幀超預算。新版 300 ms 門檻已生效，未回改舊報告或移除背景長幀；下一步先量測實際來源清單查詢的重複成本，再決定通用改造。

### E116 — 共用測試目錄增加巢狀來源後，舊筆數假設失效（R35）

EditorInventoryRegressionR35 為 14 passed、1 failed、2 ignored。中毒鎖回退測試最後預期 1 筆，但前面的 missing-root 恢復測試已在同根目錄新增第二個巢狀 ZIP，正式讀取正確回傳 2 筆。修正斷言比較整份直接來源清單，涵蓋兩筆實際內容；未改正式快取或放寬回退要求。保留失敗紀錄後重跑完整 SQL 測試。

### E117 — 來源清單變快不代表飾品總切換達到 300 ms（R35）

通知驗證的實際單來源查詢已從約 55–59 ms 降到 0.215–0.259 ms，但正式飾品仍 5154.4246003 ms／最低 6.1119539 FPS，8 項功能成功不能替代效能通過。34,396 幀保留全部背景暖載入，138 幀超預算。來源擷取 437.9242 ms、完整原生請求 968.9758 ms 等成本仍存在，未將平均 FPS 當作持續 60 FPS。exit.txt=0 而 report.valid=false，驗收腳本正確據報告拋出失敗；後續一律同時核對完整報告、案例、門檻及二進位雜湊，不只核對退出碼。

### E118 — Windows 搜尋應傳目錄與 -g，而非將 wildcard 放進路徑（R36）

Profile 搜尋誤傳 Source/OpenKoikatsu/OKGallery*.cpp，rg 回報 os error 123。改成 Source/OpenKoikatsu -g 'OKGallery*.cpp'；後續所有搜尋沿用此形式，避免遺漏載入流程。

### E119 — 診斷輸出路徑與 trace 分析限制需核對（R36）

初版分析腳本向上取錯一層，未附上交易時序；改成先 resolve 絕對標籤路徑，再從 Saved/Tests/EditorAcceptance/tag 的 parents[2] 找 Saved/AppearanceEditor，並以請求 output 精確配對。重跑兩個標籤皆附上正確交易。UnrealInsights 解析有既存 MemAlloc Invalid Tag 警告；本輪不分析記憶體，僅使用成功匯出的 CPU timing events，已核對 event 時間線與 log。匯出非同步尚未完成時曾提早讀不存在的 CSV；後續先核對檔案存在、匯出成功計數及完整退出紀錄再分析。父子 inclusive、跨執行緒累加與 wall-clock 分開，避免誤報耗時。

### E120 — Windows Python 編輯中文原始碼必須明確 UTF-8（R37）

編輯腳本部分完成後，讀取新中文 cpp 時採用系統 cp950，觸發 UnicodeDecodeError。保留已完成部分，只以 encoding=utf-8 補完尚未執行項目；不重跑有已變動錨點的完整腳本。搜尋 UE 標頭亦曾再誤用路徑 wildcard，已依 E118 改成目錄加 -g；不將找不到 API 當作不存在。

### E121 — 共享工作區建置期間其他標頭變更導致 UHT 行號失配（R37）

EditorCardScrollBuildR37 的清單新類別及使用端已編譯，但其他工作正在修改 OKStreetSocialScene.h（23:19:26）；該檔 UHT GENERATED_BODY 行號與目前原始檔不同，Module.OpenKoikatsu.1.cpp 發生 C2143/C4430 與建構式生成錯誤，建置 77.62 秒失敗。保留原 log，不回退或修改其他功能；以目前完整來源重新跑 UHT／建置，再驗收正式二進位。此錯誤不能當成清單已驗收。

### E122 — PowerShell 腳本成功不可用未設定的 LASTEXITCODE 判斷（R37）

EditorCardScrollLoadingR37 的 LoadSelection 已 Success，實際 exit receipt=0；外層誤以 LASTEXITCODE -ne 0 判斷只使用 Start-Process 的 PowerShell 腳本，空值導致假失敗、GUI 未啟動。改為尊重腳本本身例外與保存的退出紀錄，確認成功後單獨啟動 GUI，不重跑已通過測試。

### E123 — PowerShell 字串插值讓新驗收模式未接上（R37）

第一次 Cards GUI 雖驗證 ValidateSet 接受 Cards，但修改 switch 時使用雙引號使 $Mode 提早展開，未匹配原始碼，實際 launch 執行 ok.AuditEditorDemo。核對程序命令列才確認錯誤；停止唯一 log 路徑及執行檔吻合的自有測試程序，保留紀錄。改用單引號字串精確插入 Cards 分支，另開全新標籤驗收，不將 Demo 結果當成捲動成功。

### E124 — UE Editor 的 UListView 不接受直接原生 EntryWidgetClass（R37）

EditorCardScrollGuiFixR37 真實截圖顯示 EntryWidgetClass [OKCardListEntry] is not a Blueprint class，原生類別可編譯但 UListViewBase::RebuildWidget 的 Editor 檢查拒絕建立列表。保留截圖並停止僅符合唯一 log 的自有程序。透過正式 WidgetBlueprintFactory 建立 /Game/UI/WBP_CardListEntry，繼承原生列類別，編譯保存為可隨專案打包資產；ListView 引用該正式 Blueprint，不偽造 bCooked 或改引擎檢查。補上「至少有可見列」斷言，避免空列表也先通過。重新建置及完整 GUI 驗收。

### E125 — 動畫名稱列舉不應解碼所有動畫曲線（R38）

打水漂 id984 首次準備 17059 ms，其中狀態名稱列舉 15501 ms。列出名稱時對 810 MB 動畫包所有 AnimationClip 完整解碼，選單又被停用。改以 TypeTree 名稱前綴讀取，只有 additive controller 涉及片段才檢查參考姿勢；runtime overlay 替換片段仍另行驗證參考姿勢。禁止只靠逾時或顯示播放成功掩蓋載入問題。調查時再次誤用 Windows rg wildcard 及猜檔名；搜尋須目錄加 -g、先 rg --files（沿用 E118/E120）。

### E126 — 準備資料加入檔名後，既有測試須同步解構（R38）

準備資料由 SerializedFile 改成 (檔名, SerializedFile)，正式程式已改為檔名過濾，但既有作用域生命週期測試仍直接呼叫 tuple.data()/objects，造成測試編譯失敗。須同步解構並比對檔名及資料，保留原測試的釋放檢查。同輪腳本在 Tools/ok-extract 工作目錄誤用 workspace 相對路徑，修改未執行；改明確絕對路徑後確認驗收模式的 ValidateSet 及 switch 都已接上。

### E127 — 同卡重新選取後模型重用遺漏原始參數同步（R39）

SelectIndex 重新讀回卡片，但 unchanged actor 捷徑未同步 saved shape／物理／mod。另一條服裝快取路徑在 model layout 尚未啟用時套用 mod 被 guard 擋下，且漏處理直接進互動的旗標。顯式 ApplyShapes 原本也未清除先前滑桿 pending request，下一幀可能覆蓋剛還原的值。修正重用路徑與 pending 取代規則；讀卡先存入暫存成功後再發布選取，避免壞檔破壞目前角色。驗收須同時檢查 UI／Selected 與 Session、骨骼，不只檢查選取名稱。

### E128 — 共享工作區建置中外部 API 變更及 TObjectPtr 測試推導（R39）

EditorNavigationParametersBuildR39 建置途中，10:21:20 的 BodyReactionHit API／ok_extract.h 變更造成原本 makefile／編譯輸入與新 cpp 不一致，發生缺少四參數宣告與 ok_body_reaction_hit_local。目前來源已有對應宣告，待本輪結束後重建，不回退其他工作。新驗收迴圈誤用 auto* 推導 TObjectPtr 元素，改為明確 UOKShapeSlider*。保留失敗 log，只有重新完整建置後才能驗收。


## E129：外觀交易覆蓋清單原卡路徑（R39）

- 原因：外觀與顏色交易直接將 Entries 的來源路徑換成工作副本，顯示名稱仍是原卡，重新選卡會讀回編輯值。
- 修正：共用 SetWorkingCardPath 保留該列原始路徑與卡片模式，明確重選成功後才恢復來源；目錄重掃時清除來源對應。失敗讀卡不清除來源。
- 防止重犯：外觀交易只能更新工作路徑，不能丟失清單來源身分；GUI 驗收包含工作副本後重新選原卡。


## E130：GUI 驗收期間共用 DLL 被更新（R39）

- EditorNavigationParametersGuiR39 的 13 個案例 valid=true，但 runner 偵測 DLL SHA256 與啟動前不同，拒絕當成現版證據。
- 原因：另一個共用工作區建置在 GUI 執行期間完成。保留該次輸出，不算正式通過。
- 處理：確認無 UnrealBuildTool／Build.bat 執行後，以新 tag 重跑並檢查前後 DLL 雜湊一致；不終止其他工作程序。

- E130 補充：EditorNavigationPropertiesR39 啟動後 DLL 再於 10:37–10:38 更新；長時間交易尚未結束即已失去現版證據資格，確認 PID 27096 與本輪 abslog 後停止自己啟動的驗收，保留輸出並以新 tag 重跑。

## 2026-10-09 材質比對診斷
- GUI Unreal 程式不能以 PowerShell 直接呼叫後假定已等待完成；R1 截圖程序未完成。改用 Start-Process -PassThru 與 WaitForExit 並檢查退出碼。
- R2 離屏截圖受視窗尺寸限制，且穿著服裝，不能驗收身體材質。後續固定 ForceRes、尺寸與衣物狀態。
- 材質 JSON 同時有 _LineColor / _linecolor，PowerShell ConvertFrom-Json 必須加 -AsHashtable，否則大小寫鍵衝突。
- 原作 probe 已成功產生完整 renderer/material/mesh 資料（Card0.json error 為空），但原作 Quit 未在 240 秒內結束。只終止本次擁有的 CharaStudio PID 65552，並於 finally 移除臨時 probe DLL；不能把此行程退出當作成功 exit 0。
- C++ 編譯讀取原始碼時可能短暫鎖檔；第一次新增 closeup 選項寫入失敗，檢查原檔完整後重試成功。編譯完成後必須再補編此修改。
- PowerShell 的 -ModuleWithSuffix=KKUnityBundle,384914 必須整個加引號，否則逗號造成剖析失敗，編譯不會開始。
- 高光測試 R1 在專案根目錄執行，找不到 Cargo.toml；R2 改用 Tools/ok-extract 工作目錄，2 個來源測試通過。

## 2026-10-09 材質驗收 R5：suffix DLL 相依混載
- R1/R2 編譯成功，但 suffix 主程式仍 import 原名 KKUnityBundle.dll，造成原名與新版 shader module 同時載入，觸發 Shader type was loaded too late assertion。R5 無有效畫面。
- 不可把 hot reload 編譯成功視為可獨立啟動。驗收前檢查模組 import 與 manifest 一致。確認目前 UnrealEditor 無執行後，改為一般完整建置；不修改引擎原始碼。

## 2026-10-09 材質近照驗收發現
- ok.ShotPreview FOUR_VIEWS 實際產生 4 張（正面、側面、背面、臉），驗證腳本原誤設為 5，已依輸出契約修正。
- 當時曾誤判缺少來源 UV1 應填零；此推論已被下方 Native2xR3 原作 A/B 推翻。最終規則為保留 authored UV1，缺少時髮材質綁定 UV0；不得沿用舊的填零假設。
- 身體 morph shell 不可套用臉部 UV mouth-cavity 三角剔除。

## 2026-10-09 不可把髮亮帶假設當作根因
- R12 明確宣告 TBN 依賴後畫面仍缺上方亮帶，證明這不是足夠的視覺修復。
- Native2xR2 同卡原作背面照可重現上方髮絲亮帶。Mesh.uv2 空集合只能證明沒有儲存 UV1，不能證明 Unity 實際 vertex binding 一定讀零。下一步用原作 cloned mesh 的 explicit-zero / UV0 對照判定，禁止繼續由 API 空集合推斷 GPU fallback。

## 2026-10-09 原作 A/B 推翻 UV 空集合推論
- Native2xR3 的 missing UV1 與 explicit UV0 平均誤差 0.000000943/255、最大 1；explicit zero 最大誤差 33 並丟失亮帶。Unity 在此 draw binding 回用 UV0。
- 已撤回 R9 零值補齊：改為保留 authored UV1，缺少時髮材質綁定 UV0。使用獨立原作 cloned mesh 控制與 verify_missing_uv_binding.py 作為證據，避免再次把 Mesh API 空集合當作 GPU 預設值。

## 2026-10-09 R13 擷取啟動等待
- R13 卡在 Unreal 啟動（Python startup logs 之後），未進入角色載入、沒有 PNG，不能算材質驗收。PID 20240 起始 15:52:09 與本次擷取一致，核對 exe path 與起始區間後只停止該 owned process。保留 capture.log 重試。
- 驗收近照髮頂裁切，調整 opt-in 相機 focus=.78 height / distance=.25 height scale；不改 production 相機。


## 2026-10-09 三月七顏色驗收建置與路徑
- mesh_dump/[Card] March_7th_keiyuu.png 實為目錄，Get-FileHash 不能用來讀它；應讀 outfit0 材質 JSON，原始卡另由 verified receipt 定位。
- MaterialColorMarchBaseline2xR1 啟動停在 Python startup，未產生 PNG；只停止該次 owned PID 5504，不能列為有效 baseline。
- MaterialColorMarchBuildR1 因 makefile 重新產生後包含引擎 .modules metadata 而被 -NoEngineChanges 拒絕。保留限制，R2 改明確指定專案模組，避免寫入引擎；不得為了通過而移除引擎保護。
- MaterialColorMarchBuildR2 成功，但專案 manifest 仍指向已被 UBT 清掉的 -1044 DLL，R2 capture 無法載入 KKCrowdGpuPacking。已備份專案 .modules 後，核對一般建置 DLL 存在與 SHA256，只將專案 manifest 改回一般檔名；未改引擎 manifest。證據 MaterialColorMarchBuildManifestBackupR1/repair.json。
- R3 僅主光修正仍有偏白與過強白色高光，不列為顏色修復通過。
- R4 原作 skin 程式第一次接線只使用 Session.ShaderTexture，漏掉 OkMeshView 傳送的既有 skin 貼圖。ready=1 不代表貼圖完整；改為同時綁定兩種傳輸欄位，重新建置及成像，原 R4 不算視覺通過。

### March 材質對照：來源與後製驗收界線（2026-10-09）
- 原作 oracle R1 使用錯誤環境變數 OK_GAME_POSE_CARD，實際載入預設卡；正確指定為 OK_PROBE_CARD。不得用 R1 作 March 色彩驗收。
- 原作 Studio 預設 oracle 包含衣服與相機後製，不能直接與使用者無衣服、不同後製畫面做像素一致宣稱。新增 RawRender 對照並保留限制。
- Shader source metadata 是 prefab 值；卡片載入後 legacy 欄位可能覆寫它。共用 skin binder 必須接入 resolved 值，不能只換 pixel program 就視為成功。

### March 驗證編譯隔離（2026-10-09）
- MaterialColorMarchBuildR5 全專案編譯被工作區其他進行中的 OKCharacterMCP.cpp / OKGalleryWidget.cpp 編譯錯誤阻擋；本次材質檔案未出現 C++ error。
- 不覆寫其他工作中的程式，改以 -Module=KKUnityBundle -Module=KKRuntimePose 編譯本次材質範圍；仍保留 -NoEngineChanges。

### 原作探針服裝狀態（2026-10-09）
- 原作 ChaControl.SetClothesStateAll 只更新 fileStatus，UpdateVisible 在下一次 Update 套用；不能同一個 iterator 步驟立刻拍照宣稱服裝狀態一致。
- 原作 native 狀態 3 是全脫，不能把專案 normalized 狀態 2 直接當作原作全脫。
- Oracle R5 仍是穿衣畫面，不可標記為無衣服對照。修正探針等待 Update + EndOfFrame，R6 重新取樣。

### March 根因分析的排除項目（2026-10-09）
- 不得將 Studio oracle 直接當成人物編輯器 oracle。兩者 `_ambientshadowG` RGB 與 alpha 規則不同；需要同一原作 shader 的單變量控制。R7 已加入 Character globals 對照並復原所有全域狀態。
- UE5.8 有 ACES 分支不代表預設使用 ACES；實際預設 Filmic，且 LUT on/off 成像有明顯差異。未取得實際證據前不得以此改 renderer 設定。
- LUT 對照必須用相同座標三線性取樣；32³ 與16³ 的 nearest-grid 樣本不同，不能據此判定 LUT 錯誤或擅自更換共用資源。
- PowerShell 傳給 rg 的目錄參數不會展開 `Saved/Shader*` 或檔案路徑萬用字元；先列出目錄或用 rg 的 -g，避免重複無效搜尋。

### Minimalist 類型核對（2026-10-09）
- 先前把使用者所指 Minimalist 當成後製 LUT，是錯誤分類。實際 mt_ramp 清單名稱 Minimalist，resolved id=100011612，GUID=Augh.MinimalShadow，local id=1。先核對原作 UI 對應資料分類，不能只依名稱猜測。
- MinimalistNativeClothedR1 以 filter 名稱查找，找到0筆而明確失敗；沒有拿其他濾鏡冒充。R2 改擷取 mt_ramp 清單確認真正來源。
- GamePoseProbe 沒有引用 LINQ 擴充方法；第一次加入 Where 編譯失敗。改用 foreach/List 並重新編譯成功，避免為小型探針引入額外依賴。
- 新的穿衣驗證預設 ClothesState=0；normalized 與 native 狀態仍各自明確指定。
- 關閉陰影需分別處理動態投影、全域 ambient alpha 與 shader 的 _ShadowColor。R1 只停 capture／設 alpha，背面衣褶仍有陰影色；R2 增加場景層白色陰影色覆寫，不更改角色來源材質。
- 非 Editor R2 編譯遇到 FOKToonShadowParameters 彙總初始化對欄位順序的依賴；工作區同時新增材質欄位時，首參數被當成 UMaterial* 而失敗。場景初始化改為具名成員賦值，保留其他材質欄位，避免新增欄位再破壞此初始化。
- Editor R3 連結遇到 LNK1104：另一個 CharacterMCP/LiveR3 的離屏測試載入同一 DLL。不是材質 C++ 編譯錯誤；不得把別人的測試當成自己的行程終止。R2 DLL 功能驗證已完成，具名初始化後的非 Editor R3 編譯也成功。
- 全專案 build 要求修改引擎平台 .modules 而被 -NoEngineChanges 擋下；不得移除保護或修改 D:/UE5.8 以求通過。

## E131：T-Pose 陰部被衣服動態把替換身體鏈寫歪（2026-10-09）

C019（九十九 過子，來源 `KK_384914.png`）在讀卡器 T-Pose 的陰部與遊戲 dump 不符。dump 在 `KoikatuSunshine/mesh_dump/KK_384914.png/九十九 過子_0/mesh_37_o_body_a.json`。`cf_J_Vagina_B` 綁定局部是 `[8.383516e-11, 0.16992933, 1.229346e-7]`，壞掉時被寫成 `[-2.164347e-9, 0.15042137, -0.09173097]`（約 9.4 cm）。陰部島 z 最大值從 `0.03576325252652168` 被拉到 `0.05302753299474716`。新鮮 `session::open`（有遊戲目錄、會套站立 clip）與 dump 最大差約 1.2 mm，posed 與 rest 的這根骨頭一致。C000 沒有 `cf_J_Vagina_B`，不走這條鏈。

不是角色名特例，也不是權重、綁定旋轉、體型滑桿、ABMX 或 SAC 的 `vagina.open`。`shape` 不寫任何 `cf_J_Vagina_*`。BP 陰部 rig 的 rest 就是作者綁定，與 dump 一致，不能重設旋轉。

寫入者是 `update_clothes_dynamics` 的 `copy_simulated_pose`，不是身體 Ver02。Ver02（`update_body_dynamics`）只有四條胸臀鏈：`cf_d_siri01_L` kind 3 與 2、`cf_d_bust01_L` kind 0、`cf_d_bust01_R` kind 1，沒有陰部根。階段對過：body dynamics 之後 `cf_J_Vagina_B` 仍在綁定，clothes dynamics 之後才變成上面那個壞局部，export 與配件不再改它。配件拿的是 `&Skeleton`，不寫身體局部。

BP Profundis（9823 頂點，含 C019）把 13 條普通 DynamicBone 放在陰部根上（`cf_J_Vagina_Pivot_B/F`、左右 `Pivot_R/L.001`–`.005`、`Pivot_Inner_F`）。另一套 9437 頂點身體是 12 條，少 `Pivot_Inner_F`。擷取時 `dynamic_bones_for_skeleton(...).retarget_body(...).with_game_fixes(...)` 把它們併進 `clothes_dynamics`。`retarget_body` 不填 `component_name`，祖先與 overlay 維持空。衣服複本走 `retarget_clothes`，一定會寫 `component_name` 和 `component_ancestry`。重力與外力都是 `[0,0,0]`。

兩層原因，都依鏈的種類，不依卡名：

1. `configure_character_colliders` 把角色全身碰撞體，再加上 KKS 裙／地板碰撞體，配給同一集合裡每一條非 Ver01 鏈。這對 LoadCharaFbxData 的衣服複本是對的。替換身體鏈不該拿到全身膠囊。零重力下那 9 cm 是 T-Pose 大腿把陰部往後推。配件 DynamicBone 的 `component_name` 也是空的，但它們走 `configure_character_colliders`，必須繼續撞身體。不能把空名稱判斷放進那條共用路徑。
2. 只拿掉多餘碰撞之後，9823 頂點身體的 `cf_J_Vagina_B` 回到綁定，C019 清掉 ABM 的島 z 與 dump 相同。9437 頂點身體仍在 dt=0 把暖機粒子寫回去：C026 約 22.6 mm（幾乎全在 -z），C045／C048 約 8.3 mm，C051 約 7.1 mm，C063 約 3.7 mm。當時掃描 `bp_with_vagina_b=40`、`offset_over_5mm=4`。靜止預覽沒有經過時間，不該用暖機結果蓋過剛同步的姿勢。衣服複本仍要寫回，裙子才會在預覽垂下。dt>0 仍寫回所有鏈。鏈本身不關，作者的 `m_Colliders` 也不清。

修正在 `Tools/ok-extract/src/dbone.rs` 與 `session.rs`：

- `configure_shared_clothes_colliders` 只給 `update_clothes_dynamics`（初次、240 步暖機、最後一步）。空 `component_name`、空 `component_ancestry`、`overlay_bindings` 為空的鏈清掉外來碰撞，只留作者植入的碰撞體與作者 freeze axis。
- `copy_simulated_pose_filtered(body, dt == 0.0)`。dt=0 時跳過上述替換身體鏈，再 `rebuild_worlds`。dt>0 與舊的 `copy_simulated_pose` 仍寫每一條。

空 `game_root` 時略過 Ver02 的 dt=0 迭代留著。它對齊 IllusionFixes，量過不是陰部的寫入者。不要再把它寫成這次修復。

驗收是 Rust 的群眾卡數值，不是讀卡器截圖：`cargo test --release --lib c019_crotch_gap_diagnostic -- --ignored --nocapture`。通過條件是每張有 `cf_J_Vagina_B` 的卡 posed 對 rest 位移 < 2 mm，C019 島 z 與 `0.03576325` 差 < 3 mm，C000 仍是 9558 頂點且沒有這根骨頭，`offset_over_5mm=0`。

2026-10-10 重新跑過，exit 0，33.46 秒。`CLEARED abm` 的島 z 最大值是 `0.03576325252652168`，與 dump 相同。`C019.png` `vagina_b_dp=0.0000`、`zmax=0.0358`。9437 頂點的 C026／C045／C048／C051／C052／C063 也是 `0.0000`。`bp_with_vagina_b=41`（多了 C0000.png）、`offset_over_5mm=0`、`torn=0`。C000 仍是 9558 頂點、`zmax=0.0359`、沒有 `cf_J_Vagina_B`。C019 對 C009 的陰部 rest 位移是 0。

同一次驗收的第一次編譯失敗，不是陰部回歸。`portable_hair_soft.rs` 當時正被別的工作寫到一半，浮點字面少了整數部（`.9999`、`.025`、`.001`、`.5`），型別也推不出來。不要從那個錯誤改陰部。檔案寫完後原樣再編，沒有改那份頭髮程式，測試就過了。

`ok_extract.lib`（12:16，97,382,712 bytes）裡看得到 `configure_shared_clothes_colliders` 與 `copy_simulated_pose_filtered`。12:18 的 `UnrealEditor-KKUnityBundle.dll` 是在這份庫之後連結的。正在跑的讀卡器日誌 `Saved/Logs/CardReader/20261010-002633752-04d00d70.log` 印出 `ok_extract.lib stamp 2026/10/10 12:16`。沒有為了重連去關掉 CharacterMCP LiveR8 或這次讀卡器。

驗收通過之後 `cargo build --release --lib` 與 `build-release.cmd` 又跑過。`dbone.rs`、`session.rs` 的陰部規則停在 12:19，沒有再改。靜態庫後來被別的 Rust 一起更新，`OKExtractBuildStamp.h` 現在是 `2026/10/10 12:32`。`UnrealEditor-KKUnityBundle.dll` 仍是 12:18，讀卡器行程還握著它，沒有關掉 CharacterMCP 或讀卡器去重連。12:32 戳記要等那些行程結束、下次 Editor 建置才會進 DLL。在那之前的截圖不能拿來證明 12:32 的庫；12:18 的 DLL 已經含上面兩個函式。

Editor 靜態連結 `Tools/ok-extract/target/release/ok_extract.lib` 進 `UnrealEditor-KKUnityBundle.dll`。只跑 cargo 時，已載入的 DLL 仍是舊碼。`OK_NOCACHE=1` 不會換掉已連結的程式。截圖要等該 DLL 重新連結、編輯器重新載入之後才算數。

不要重做的錯誤：

- 依角色名分支。
- 夾體型滑桿、把 SAC `vagina.open` 套到 BP、重設 `cf_J_Vagina_*` 綁定旋轉。
- 改 `DriveVaginaOpening`／`SpreadVaginaPullBones`（插入用，`Open01 <= 0` 直接返回）。
- 用正面縫隙數字，或「別張卡看起來沒破」，代替 `cf_J_Vagina_B` 位移。
- 關掉全部身體動態。胸是 Ver02，會一起凍住。
- 為了陰部重擷取精簡卡。bra id 16 會變回原版連身衣。
- 強迫套站立 clip 來掩蓋讀卡器 T-Pose。新鮮擷取之所以沒下垂，是因為有遊戲目錄且沒設 `OK_UE_ANIM` 才先套 clip；讀卡器是空 `game_root` 的 T-Pose。
- 把空名稱判斷放進配件碰撞設定。
- 第一次只修碰撞就當整群通過。Profundis 的 `cf_J_Vagina_B` 回來了，9437 頂點身體還有零重力沉降。
- 把空 `game_root` 的 Ver02 略過寫成根因。Ver02 沒有陰部根；body 步驟之後骨頭仍在綁定。

### 讀卡器啟動依賴遺失（2026-10-09）
- 使用者執行 CardReaderDevelopment.cmd 時，Binaries/Win64/UnrealEditor.modules 不存在，啟動腳本第 21 行直接中止。DLL 存在不等於可啟動；先前只驗證離屏材質輸出，沒有在最後一輪編譯後重驗實際啟動入口，驗收不完整。
- 排查期間其他編譯已重新產生專案與外掛清單，OpenKoikatsuEditor.target 與引擎 UnrealEditor.modules 的 BuildId 均為 f6d41cac-32cf-4bc1-a036-30e0b6130188。尚無足夠證據把清單遺失歸因於某一次特定 build；不能把推測寫成已確認原因。
- 加入 Tools/ensure_editor_module_manifests.ps1，讀卡器啟動前核對 Development/Win64/Editor 收據、引擎 BuildId、收據列出的專案 DLL 與現有清單。全部驗證通過後，僅從 UBT 收據重建遺失的專案清單；拒絕版本不符、DLL 遺失、越界路徑或帶 hot-reload 尾碼的重建。保留現存清單，不選時間最新的 DLL，不修改引擎。
- Tools/test_editor_module_manifests.ps1 使用獨立 Saved/Tests 小型 fixture，驗證遺失清單重建、重跑不改寫、BuildId 不符拒絕、DLL 缺失拒絕。結果：Saved/Tests/ModuleManifestRecovery/9c6e14ade22b4d9fafe793f5a11963c1/result.json，全部通過。
- 實際啟動 run_card_reader_development.ps1：PID 26728，視窗 OpenKoikatsu (64-bit Development PCD3D_SM6)，Responding=True；日誌確認 OpenKoikatsuGameMode、Load map complete、Engine is initialized，未出現 fatal／module load failure。本次啟動約 34 秒，僅驗證啟動修復，不宣稱達成載入時間或場景效能目標。
- 啟動日誌：Saved/Logs/CardReader/20261009-234541675-f5a2a8fb.log。此次沒有修改 D:/UE5.8。

### 圖片式外觀套用共用流程（2026-10-10）
- 眉型請求不是縮圖沒讀到：原先每次換眉型都重建七套服裝與所有模型。本次重現完整流程成功，但耗時 291.407 秒；使用者畫面上的「正在套用」不能當成已完成。直接目錄貼圖改為更新每套服裝對應的貼圖參照，保留無關模型／骨架／貼圖；不依卡名修正。
- 上眼睛高光的來源 shader 使用 `_overtex1`，先前衝突清理漏掉它，舊 MaterialEditor 高光會蓋住新選擇。清理需涵蓋實際 native property，同時保留 `_overtex2` 與瞳孔 MainTex。
- 左右瞳孔漸層共用 category 409，不能僅依 category 找第一筆 resolver；必須使用 PupilGradient1／PupilGradient2 的 property 身分。
- 花紋、徽章、服裝子部件都是 Coordinate 編輯；不能僅靠 target 名稱以 clothes 開頭決定是否只更新一套服裝。重建範圍改依 registry 的資料區塊。
- 第一輪 GUI 驗收報告雖 valid=true，但畫面顯示換外觀後衣服位置異常。既有測試只比 body 頂點數及貼圖，沒有驗證骨骼狀態／實際 posed geometry，因此不能把 R1 報告當最終驗收。
- 根因：legacy APNG 的外觀來源還原，把原始 KKEx 的 KKABMPlugin.ABMData 重新注入目前編輯狀態；原始卡中的 ABMX 與已正規化 APNG 的空 modifiers 並不相同。缺少 plugin record 在此代表目前無 modifier，不能拿舊原卡補回。來源還原現在保留目前卡的 ABMX／PregnancyPlus 原始記錄（含刻意缺省），其餘外觀來源與未知插件資料仍保留。
- 新增 legacy C001 七套服裝的回歸：換眉型前後所有 parts 的 rest_positions、positions、render_visible 完全一致；另驗證 C000 七套服裝的眉毛貼圖與完整來源重建一致。兩項已通過。GUI 程式增加換外觀前後 EditorModsJson 一致性檢查。
- 測試工具縮圖請求上限 48；第一次送 53 分類被正確拒絕，之後依 UI 的 32 張分頁分兩批完成，53 個代表縮圖均有有效路徑。不能為測試放寬實際安全上限。
- PowerShell 不展開 rg 的路徑萬用字元；本次再次遇到 `src/portable*`／`Tests/*` 無效路徑，應使用確切目錄搭配 `-g`，避免重複。

### 限定模組編譯後的啟動 metadata（2026-10-10）
- 本次明確重現：`-Module=KKUnityBundle -Module=OpenKoikatsu` 編譯連結成功，但 .target／.modules 仍缺失，不能只把 Build Succeeded 視為可啟動。
- 新增 publish_editor_project_metadata.ps1：使用 UBT 原生 WriteMetadata，僅允許經檢查的專案 TargetMetadata.json；Version 必須為 null、全部 manifest／receipt／load-order 輸出在專案範圍、DLL 存在、引擎 version／modules BuildId 一致。不得執行 EngineMetadata.json 或修改引擎版本檔。啟動 preflight 在 receipt 缺失時走此流程。
- 已執行專案 metadata 發布並檢查引擎 UnrealEditor.version 前後 SHA256 一致；正常啟動 DryRun 通過。
- R2 連結再次遇到其他運行中讀卡器／CharacterMCP 持有 DLL 的 LNK1104。保留載入中的原檔並移至 Saved/BuildBackups/AppearanceSelectors20261010-003626，記錄原路徑與 SHA256，讓重編譯建立新磁碟檔；沒有終止其他行程。保留備份直到使用舊 DLL 的程式結束。

## 2026-10-10 圖片選擇逐筆 300 ms 驗收與修正紀錄

- R2 的八項原始／模組素材、保存重載、重置及換裝 GUI 驗收通過；人工檢視 report_mod_applied.png 確認黃色洋裝位置正常。這僅證明原功能修復，不代表新效能要求通過。
- 新要求必須依 UI 順序，每類連續十個不同素材，每筆從按鈕 callback 到 render readiness <300 ms；不足十個必須列出可用數，診斷子集合不得當完整驗收。
- 實測第一次頭型選擇 526821.870 ms，實際模型、選取與保存來源驗證成功，但效能失敗。已保存 AppearanceSweep20261010Baseline/report.json，停止這輪診斷，避免重複跑十分鐘級重建；完整順序驗收仍未完成。
- 根因之一：外觀選擇先等待保存完整 APNG，再完整輸出七套來源資源、讀回與重建預覽。材質貼圖變更也可能重新產生 transient shader graph。不能只優化 UI 選取色或把點選後的準備時間扣除。
- 原生眼睛的上下高光是獨立 draw texture；只更新 iris MainTex 仍可能看不到變化。已補眼睛 RT 及 eye_texture_0/1 更新。七套服裝與完整原生來源逐套比對，RT、高光及原始 UV 一致，測試通過（eye-native-1791565002118947000/acceptance.json）。
- 全 outfit 貼圖修改原本重包完整 unique 資源七次；新增 batch transaction，只重包一次，要求與序列重包結果完全相同並拒絕重複 outfit index。
- 新 GUI sweep 的初次編譯錯誤：TArray<TObjectPtr<T>> 不能用 auto* 直接推導；TObjectPtr 沒有 IsValid()。已改明確 UOKAppearanceButton* 及 IsValid(Ptr.Get())。
- native 比對測試執行期間，cargo 重新連結同名 test EXE 會 LNK1104。保留原執行檔為獨立 test-binaries/eye-native-r4.exe，後續長測應先複製到唯一命名路徑再執行，避免編譯輸出被執行程序鎖住。
- PowerShell 的 rg 路徑不能依賴 shell 展開 *；應指定真實目錄並用 -g 過濾。不要再用 Source/OpenKoikatsu/OKGallery* 當檔案參數。

- HotTextureR1 初始載入停在 progressive parent readiness，未進入任何點選。原始碼顯示 Ready 只檢查尚未被 render 使用的 parent，沒有提交 UE on-demand shader compile；先等待 parent 才能綁到 component，形成相依循環。改為 readiness 檢查主動提交未完成資源的編譯，使用引擎本身同 priority 去重。不得把未開始的測試視為點選失敗或通過。WPR CPU 採樣因目前權限無法啟用（0xc5585011），未更改權限；改用短暫執行緒採樣並保留結果。

- Sweep R2 的初始 progressive 載入已前進，最終材質 pending 歸零；測試仍等待全域 shader manager（包含未顯示的背景換裝資源），這個 gate 不等同目前人物可顯示完成。驗收改為要求 active actor 的 PreparePlaybackMaterials(false) 與最終材質完成，加上既有 render fence；未降低材質正確性門檻。加入 stage／busy 原因的十秒進度紀錄與起始報告，避免未開始測試時沒有診斷證據。R2 已停止，無點選通過證據。

- HotTextureR3 第一筆眉型點選 76766.795 ms，preview_session_prepare_ms=68171.886，來源寫入約 2.218 秒且 geometry_rebuilt=false。新增的 worker preparation 誤用非空 GameRoot 呼叫 FOkSession.Open，該 API 明確選擇原作來源擷取；即使輸入 APNG 也不會讀取既有 snapshot，並使 cf_O_face layout 與 live portable 不符。已改為空 GameRoot 讀取剛產出的 portable APNG；這是本次新增程式的錯誤，需實際重跑確認增量 commit，不能把 source cache 命中當作修好。

- Runtime head candidate 測試在上眼線 cf_O_eyeline 拒絕，原因是 C001 的這個 renderer 使用 MainAlpha（shader_kind=11），眉型的十筆通過不能推論所有 head renderer 同一 shader。已讓 native MainAlpha 的 MainTex 成為 MID 參數；APNG 與 live transaction 同步更新 png 與 shader_textures:_MainTex，避免只改便利預覽貼圖而原生 shader 仍讀舊圖。需重新跑 native／GUI 及畫面驗收，不得只放寬 shader_kind 判斷。

- HeadTypesR1 七類各十筆完成，功能／保存 70/70 成功，速度 0/70 達標；不能因局部 commit 約 10 ms 就宣稱切換小於 300 ms。報告保留原始端到端時間。
- HeadTypesR1 啟動雖指定 1600×1000，實際截圖為 888×500。後續驗收必須核對 viewport，而非相信命令列參數；加入啟動 ForceRes、實際尺寸 gate 與逐筆紀錄。既有低解析圖片不足以判斷細小眼睛品質。
- MainAlpha 的 `_OKNativeAlphaOpaque` 是依貼圖證明的 blend specialization。只換 PNG 可能使原 opaque 分支對新透明圖錯誤遮擋；必須拒絕不相容增量並走完整材質準備，不能只比較 shader_kind。
- 白色 tint 原本先 decode 才判斷不需 tint；眼白原本對每個像素重算固定顏色的 sRGB 冪次。已以原公式逐 byte 等價測試約束 LUT，避免用近似 gamma 或任意調色換效能。重複轉換 cache 需包含來源內容、所有色彩參數與 operation 標記，並限制記憶體，不可僅依卡片 ID。
- 驗收尺寸修正首次編譯失敗：ResizeFrame 屬於 FSceneViewport，不能從 FViewport 呼叫。改用引擎 r.SetRes 要求尺寸，仍以 FViewport::GetSizeXY 核對實際結果；不能因加上命令就假定解析度正確。
- 新增逐 outfit checkpoint 比對首次失敗於 residue bytes；snapshot 包含 HashMap，重新序列化的 iteration order 不固定。改為解碼後全部欄位的 canonical JSON 語意比對，資源 refs 仍逐項排序核對完整 content id；不刪除 renderer/source 欄位來掩蓋差異。需重跑確認是否另有真正差異。
- R15 語意比對進一步發現 outfit 4 有真正 renderer state 差異：不能因 face 欄位是 global 就假設 CardInfo 的所有輸入與 outfit 無關。已撤回「七套只解析一次 CardInfo」優化，保留以完整 PNG 與參數為 key 的純函式快取；須重跑所有 outfit 一致性。HeadTypesR2 使用含此錯誤的 build，不能當作最終保存驗收。
- 更正前項根因：撤回單次解析後 R16 仍失敗；R17 精確欄位診斷指出 `/session/parts/51/runtime_accessory/implanted_bones/0`。這個欄位及 removed_accessory_bones 原本是 HashSet，JSON 會輸出隨機順序 array，並非骨頭 ID 被修改。驗收僅排序這兩個已知 membership set；其他 bone／weight／vertex／UV 等有序 array 保持逐項比對。先前「真正 renderer state 差異」判斷過早，不能當生產問題根因。恢復單次 face 解析後重跑完整七套語意與資源比對。
- R18 恢復單次 face 解析後，七套獨立解析／renderer 全欄位／資源 ID 全部通過，確認前述差異是驗收 HashSet 順序問題。R3 實際解析度 1600×1000，70/70 功能成功但 0/70 速度達標，必須保留未完成狀態。
- 原眼白 bake 的輸出尺寸錯跟來源 PNG；原作 ChaControl.InitBaseCustomTextureEtc 明確建立 high=512、low=256 RT。改為先線性化取樣再合成，不能直接縮小已經 sRGB 合成的圖片。處理來源 row-order 時原本會完整 encode 翻轉的大 PNG 再 decode；改成取樣 row index 反轉並以兩方向逐像素 oracle 比對。避免來源解析函式連續載入同一貼圖兩次。

- R4 眼白 GUI 十筆仍全數速度失敗。刪除大圖翻轉與固定原作 RT 尺寸沒有移除完整 APNG export 關鍵路徑，不能以 native 或提交時間代替實際點選到畫面時間。
- 眼白原本取得來源 PNG 後再次載入 bundle 查色彩空間；需在來源解析時保存其 import metadata，而非再次按名字查詢可能不同的來源。loose PNG 不可套用同名 bundle 的 metadata。
- 臉部近照首次 ShotPreview 啟動因 KKCrowdGpuPacking module 暫不可載入而退出，不能把沒有圖的啟動算驗證成功；保留 `eye-white-r4-closeup.log`，核對 project/plugin manifest 及 DLL 後以新輸出目錄重試，沒有停掉其他工作程序。
- 本輪首次讀取 KKPreviewActor.cpp 錯用 Source/OpenKoikatsu 路徑；此檔實際在 Plugins/KKUnityBundle/Source/KKUnityBundle/Private，後續先用 rg --files 找實際位置。

- R23 native 測試編譯被 HairMesh 新增 hide_source_accessory 欄位的舊測試 initializer 擋住（E0063），不是 candidate API 編譯錯誤。只在原測試補 false 預設，R24 編譯與相關測試通過，保留其他工作中的頭髮實作。

- R24 Unreal C++ 編譯完成，但兩個 canonical DLL 連結遭 CharacterMCP PID 63484 映射而 LNK1104：KKUnityBundle 與 OpenKoikatsu。核對該 PID 完整 command line、Module.FileName、workspace 邊界及 SHA256 後，僅把這兩個檔案移至 `Saved/BuildBackups/AppearanceCandidate-20261010-025105/`；保留 receipts 與使用中的程序。R24 是失敗建置，須以重新連結成功後的版本測試。
- 背景 checkpoint 與舊 appearance request 的 export 都會暫改舊擷取器的 process-wide renderer options；不能讓不同 context 同時 export 再交錯 restore 環境。export_edited 加入共同 guard，candidate 的純貼圖準備不持有這個 export guard；仍須遵守 handle 銷毀前 join 所有工作，live session 只在擁有執行緒提交。

- 把每筆素材準備派給任意 UE pool worker，不能假定前一筆 Unity bundle cache 仍可命中。ParsedBundle/RefCell cache 是 thread-local；R27 量測顯示 UE 提交約 5–10 ms、來源 worker 卻數百至數千 ms。改固定 context source owner，並用不同 caller thread 測試確保共用同一來源執行緒；不可把 RefCell 強行標示 Send/Sync。

- 素材索引需保留大小寫不敏感第一個重名 Texture2D 的解析順序，不能用 HashMap 的覆蓋插入改變來源；加入舊線性查詢的 PNG／色彩空間／sampler oracle。新增索引會在既有 serialized read 中觸發初始化，因此 with_serialized 不能每次無條件 borrow_mut；初始化完成後只取唯讀 borrow，並增加巢狀空 bundle 回歸測試。
- R31 啟動 DryRun 正常輸出 JSON，但外層命令誤用 $LASTEXITCODE 判斷 PowerShell 腳本成功：腳本 return 不保證設定 native exit code，null -ne 0 會誤報。已核對 DryRun 成功結果，再執行實測；不可把這次外層錯誤當作程式啟動失敗。
- 在大型來源 bundle 的候選名稱迴圈中使用 exact_png_from_bytes(b.clone(),name)，會讓不存在的頭型 suffix 也複製整份來源；原始上眼線 bundle 約 148 MB。cache 命中不代表 caller 沒有先做昂貴複製。改借用 bytes，再用原始素材 oracle 與連續套用／全 outfit 保存測試確認結果。
- R38 首次編譯 E0425：unity.rs production imports 只有 Path，PathBuf 僅在 tests scope 可用；新 thread-local path index 改用完整 std::path::PathBuf。保留失敗 log，以 R39 重新編譯／驗證。cache 回歸另加入相同長度但 mtime 改變的來源替換。
- 補寫紀錄時錯在 Tools/ok-extract workdir 使用 Docs 相對路徑，未寫入任何檔案；改用完整專案絕對路徑。命令內有切換 workdir 時，文檔與證據路徑必須明確。
- 避免直接 rg 輸出單行大型 catalog JSON：一次輸出會包含整個 33 MB 目錄，後續僅使用 bounded matching 或解析後挑選欄位。PowerShell ConvertFrom-Json 預設拒絕空字串屬性名稱；遊戲 list extra 可合法含空鍵，解析此類目錄應使用 -AsHashtable 或標準 JSON parser，不能將它當作素材損壞。

- R44 profile 插入腳本使用相對路徑但 exec 工作目錄是 Tools/ok-extract，造成 FileNotFoundError；沒有改到原始碼。改用完整 workspace 路徑，並在腳本失敗時 throw，避免後續 cargo 成功掩蓋編輯失敗。

- R45/R46 shared buffer 重構編譯發現 dbone 與 references test 仍依賴 Vec，及 test 子模組 super 層級錯誤。保留舊 Vec hang 路徑，shared consumers 改用完整 crate::unity helper；必須 cargo 完整編譯與 byte oracle 通過後才能部署。R45 shell log 路徑也改用絕對路徑，避免 cwd 不一致。

- R48 lazy container 的 files 是空的：vendor parse_directory_lazy 只填 nodes，files 要 eager parse_files 才填。原始素材 oracle 與同序測試立即發現零貼圖／來源解析失敗，未部署。R49依已驗證的directory nodes建立file views，保留界限檢查；不能假設 eager/lazy metadata欄位完全一致。

- R49原始PNG oracle誤通過但live-head實測失敗：nodes is_file旗標不包含部分.resS，然而舊eager parse_files保留全部nodes；新舊png reader共用缺少stream的files，產生None==None假陽性。R50保留原目錄順序全部nodes；oracle改為獨立eager container/full objects，逐張必須Some PNG，增加CMS模組、檔案首尾range／越界／overflow驗證。

- R52候選卡片ABI測試把不足容量buffer的預期值誤寫成required length；既有copy_resource契約為不足容量回傳-1且不寫buffer。測試已攔截，待R53原生編譯結束後修正並重跑。另外候選raw實際約43.7MB（KKEx含原始材質資料），不能假設是小型compact metadata、不能直接據此宣稱正式UI可<300ms。

- R53 deferred preview fixture誤把MakeShared回傳TSharedRef的Get()當pointer；UE TSharedRef::Get回傳reference，與TSharedPtr::Get不可直接比較。改用&Deferred.Get()，介面本體與writer已編譯。必須完整rebuild再跑保存失敗／retry回歸。

- R55 link LNK1104：既有user-owned CharacterMCP PID63484映射canonical KKCard.dll。Primary核對workspace絕對路徑、映射模組與SHA256後，將該單一DLL移至Saved/BuildBackups/AppearanceDeferred-20261010-042240，保留manifest與receipt，不停止現有程式、不移動任何引擎檔。R56重建成功，canonical DLL已補回。

- R56 automation在新fixture的Body.ShapeValueBody[0]觸發array bounds：EnsureMinimalKksCard刻意沒有body shape array，不能假設讀取後有44筆。R57先建立並保存44筆shape baseline，再驗證pending單通道編輯；當次crash保留log，不當成通過。

- R57新deferred save、EmbeddedPngSave、ShapeRoundTrip及AppearanceCardTransaction都通過，但prefix篩選額外跑到PortableSession；該測試要求單套、無source animation的converted fixture，C001實際七套並帶五個source clips，造成三項fixture期待失敗。改用四個明確test names；automation TestExit exit=0不能代替讀取report.failed/notRun。

## R59 工作目錄與報告路徑
錯誤：在cargo工作目錄執行repo相對report/docs路徑，導致找不到檔案，未寫入。修正：report/docs命令改絕對路徑，cargo保留正確workdir；之後不可在同一命令混用不同root的相對路徑。

## R59 oracle patch定位
錯誤：以重複的ensure_tex_catalog行作patch anchor，完整oracle斷言落到empty/nested測試，編譯E0425。修正：移至明確具名installed oracle函式內，修改後讀回位置再編譯。

## R63 automation環境變數
錯誤：設了不存在的KK_TEST_CARD，AppearanceCardTransaction需要OK_TEST_WARM_CARD因此讀取空路徑失敗，EmbeddedPngSave缺OK_TEST_PREVIEW_PACK只warning跳過；新selective test也只用minimal fixture。修正：runner提供兩個實際需要的環境變數且以C001副本執行；selective test明確讀OK_TEST_WARM_CARD，核對43MB級KKEx與完整parser結果。不可以process exit 0或success-with-warning推定真卡驗收通過。

## 2026-10-10 R65：完整APNG journal 不能當成快速互動交易

- 錯誤假設：移除完整renderer重建後，每次先寫完整APNG再reader/writer即可達0.3秒。正式GUI70筆皆超時，journal、讀卡、再保存佔大宗。
- 防止重犯：native prepare／actor診斷不等於正式UI延遲；保存一致性應透過不可變revision與保存屏障驗證，不能忽略108MB讀寫成本。baseline C001無KKEx時，conditional大型KKEx oracle沒有執行；新增實際108MB輸出副本回歸才取得40.5MB KKEx的證據。

## R66 Unreal build範圍

- full target即使-NoEngineChanges也會規劃重寫engine manifests，被UBT保護拒絕；此操作未通過，不能宣稱成功或解除保護。改回-Module=KKCard -Module=KKUnityBundle -Module=OpenKoikatsu，UBT仍自動重建受影響的project KKRuntimePose依賴並成功。保留兩份log。

## R68 borrowed MessagePack API

- 初版使用Value::as_str介面讀取ValueRef，rmpv1.3.1的ValueRef沒有此方法，E0599兩處。修正為明確match ValueRef::String後讀Utf8StringRef::as_str；未編譯版本不部署。保留native-build-r68.log，R69重新編譯／獨立oracle待驗證。

## R66 native測試filter錯誤

- owned FFI filter誤打成不存在的owned_runtime_candidate_and_checkpoint，Rust回報0 tests仍exit0。已讀取log發現並以owned_candidate_and_checkpoint_lifecycle重跑，實際1項通過4.77秒；保留corrected與原始log，沒有把0項計為驗證成功。
- 新增run_appearance_native_regressions.ps1：具名exact filters、各群minimum passed、0failed、binary SHA256一致才接受；不能只看LASTEXITCODE。

## R72 installed oracle需要ignored參數

- 新runner把真實Texture2D oracle當成普通test，只有0passed/1ignored；minimum passed檢查正確拒絕此結果。設定installed=true使runner加--ignored，以fresh NativeR72B tag重跑全組。不得刪除failed NativeR72證據或略過minimum檢查。

- R74 borrowed KKEx dictionary：Utf8StringRef::as_bytes 的借用生命週期綁定臨時ValueRef，不能回傳到區塊解析外（E0515）。只對實際Binary欄位借用；String等legacy編碼保留既有owned fallback，避免unsafe延長生命週期。失敗保留native-build-r74.jsonl；修正後必須新標籤建置及回歸。

- R78 review：named PNG的精確快路徑不能繞過原本tex_name_rank對空字串／thumb的排除。已明確要求rank==1000、保留重複名稱fallback，並加入cold named/fuzzy對eager legacy的獨立比較（空、thumb、大小寫、多候選、前綴）。新原生建置前修正，避免只驗PNG忽略來源名稱排序。

- R81 GUI sweep batch capture建置C2446：MakeShared回傳TSharedRef，不可與nullptr放在同一個條件運算式推導共同型別。改先宣告TSharedPtr，條件成立才指定MakeShared；失敗log ue-build-ui-r81.log保留。此次build失敗不能啟動acceptance，必須完成新一輪建置後再驗。

## 2026-10-10 R83 嚴格速度驗收未通過
- 七類連續十筆測試69/70<300ms，首次上眼線364.49ms；左右虹膜／漸層不能由原生平均推定GUI達標，左眼20筆全部超標。
- 保留失敗輸出和逐筆profile。不得以平均<300ms、save成功或head子集合替代「每種類型各十筆全部<300ms」。下一步處理PNG與sampler的重複整包讀取並重新正式點選驗證。

## 2026-10-10 R86 source parser identity
- 發現path cache在mtime變更後仍經sampled content-key parser cache，未取樣位置的相同長度修改可能命中舊parser。
- 改以canonical path與完整bytes做來源key；新增2048-byte fixture只改index600（不在legacy samples），明確證明舊sample key相同但新的parser不可共用。待原生與正式GUI回歸。

## 2026-10-10 R90 逐筆速度仍有失敗
- runtime shared directory初始化後首次pupil已244.626ms，但第二次375.376ms；20筆只有19筆達標。不能只驗首次或平均，也不能把資料解析成本從click-to-render排除。
- PowerShell讀取含空property name的catalog JSON需ConvertFrom-Json -AsHashtable；rg查詢Windows wildcard路徑應改父目錄搭配-g pattern，避免將未展開的*當成字面路徑。
- R91編譯啟動後又補改了來源／測試，該輪artifact不得代表最終source；完成後以固定來源重建R92並使用唯一hash測試，建置期間不再改Rust來源。
- 候選snapshot內有HashMap，反序列化後重新bincode的順序不保證相同；改以完整residue semantic value、排序references與resolved resource bytes核對，保留資料內容的嚴格一致性，不用隨機map順序判定失敗。

## 2026-10-10 R92 unordered bake-cache key regression
- 將完整source_material直接bincode做key，含HashMap時不同outfit deserialize產生不同迭代順序，相同material未命中cache。功能oracle通過但pupil平均215–250ms／gradation224–267ms，較前輪明顯退步，故未部署。
- R94只對實際覆寫的colors／floats轉BTreeMap後形成canonical key，保留source resource ID／part shader／eye presence等依賴。新增material_resolves分項與實際C001跨outfit重用斷言，避免只驗結果忽略去重退步。
- R93編輯命令指定cargo子目錄卻使用repo相對路徑，Python找不到來源。該輪是未改動R92的no-op build，不作修正證據。編輯與build分開指定正確cwd，PowerShell設Stop並檢查Python LASTEXITCODE。
- R94回歸新增斷言錯把「需準備的program數」等同「最終PNG有改動的delta數」：上眼線會準備kage與eyeline，kage結果未變時只有一個delta，因此2 resolves／1 delta屬正確行為。R95由各outfit原始residue、source reference及實際category選擇規則，使用獨立canonical JSON計算distinct programs；嚴格核對resolve一次／program，不以changed-delta數猜測。
- R96 builder抽出後函式參數Source與原本內層vertex index Source同名，MSVC C4457警告；建置成功後將內層改SourceVertex，未改索引邏輯，再以R97重新建置。

- R99 fixture compile E0308: rmpv Value::String requires Utf8String; fixed explicit .into(). First repair command used repository-relative paths with native cwd and failed before edits; subsequent cargo still ran because ErrorActionPreference was not Stop. Use absolute edit paths plus Stop for build wrappers. No production files were overwritten by failed command.

- R100 outer PowerShell wrapper checked stale LASTEXITCODE after a PowerShell script, falsely reporting Card regression failed. The runner validated UE exit0, all seven expected tests, no failed/notRun, and binary hashes; summary valid=true. For PowerShell runners rely on terminating exceptions and their explicit summary, not inherited native LASTEXITCODE.

- R102 audit review found FScreenshotRequest enqueued after item0, while the next selection could start before GPU readback/PNG output completed. Recent failures cluster at item1, but causality is not yet proven. Guard the next audit click/save until OnScreenshotRequestProcessed (engine broadcasts after ProcessScreenshotData), verify image file exists, and report audit_screenshot_ms separately. This pins evidence to its revision and avoids test-only capture overlapping the next production measurement; ClickAt and strict <300ms remain unchanged. Prior failures are preserved.

- R104 structured stage timings proved the production stall: lower-eyeline item0 click462.919ms; detail.log stderr write356.780ms; catalog texture6.248ms/flip1.221ms/tint1.339ms; detail.resolve366.200ms, source texture phase375.888ms/CPU15.625ms. Do not attribute low threadCPU/high wall to OS load or asset decoding without measuring synchronous diagnostics. Per-asset success text now requires OK_DIAG or OK_APPEARANCE_PROFILE; errors remain and JSON phase telemetry remains always available.

### R105 來源一致性測試基準錯置
新增 filename-free runtime 來源比較時，使用 session::open（含既有 SQL 快取與後續加工）作為原始抽取基準，bind poses 比較失敗。改為 established extract_session 的全新抽取路徑，隔離快取／後處理與 filename-free 輸入的差異；尚未通過前不得聲稱來源等價。大型矩陣斷言改為帶 renderer 名稱的精簡失敗訊息，避免巨量輸出遮蔽診斷。既有失敗 log 保留。

R108 彙整 guard 初版直接比較 JSON object 序列化順序，誤報 binaries mismatch；改為依 binary 絕對路徑排序後逐項核對 path/hash，實際三輪 hash 相同。JSON object 屬性順序不是資料契約，不可用於一致性判定。

### R113 geometry candidate / APNG editable shape semantics
Installed context integration failed at validate_geometry with "geometry candidate shape inputs changed"; compatibility rejection correctly prevented publication. Root: source extraction uses shape plugins (e.g. PushUp), while portable::load_outfit explicitly restores editable Custom values and coordinate-local ABMX/bust settings. preview_globals contains source extraction state and cannot be treated as the displayed runtime state. R115 extracts the existing APNG editable-shape normalization into one shared helper, keeps load behavior unchanged, and applies it to the owned candidate before reference export. Source accessory context and saved snapshot retain source semantics; candidate rendering uses APNG display semantics. Do not remove shape/rig validation or special-case the fixture/card. Regression must pass add/stale/checkpoint/reset with the original shape-plugin card.

R115 APNG integrity command initially used the wrong module name portable::tests instead of portable::reader_regressions; executable reported zero tests. Zero matches are not acceptance. Correct filter discovered with --list and completion must require an explicit nonzero passed count; original zero-match log retained.

R115 corrected legacy 100-card integrity test could not start: its required output/CrowdAPNG/C000.png fixture no longer exists. This is a missing historical fixture, not a production pass or a newly proven rendering failure. Do not manufacture that directory or weaken its receipt checks to obtain a green result. Keep both logs. Add a separate installed APNG runtime semantic regression, parameterized by OK_TEST_PREVIEW_PACK, using the current actual C001 fixture by default: every available outfit must preserve editable shape/bust inputs, finite geometry, valid indices, stable private-rig refresh and prepared reference-array counts, and leave source bytes unchanged. Explicit nonzero test count required.

### R119/R120 content-addressed ID 依 HashMap 順序漂移
新增 geometry C ABI lifecycle 時，worker thread 準備的 accessory checkpoint 與主執行緒 native 準備結果的 `accessory_model`／`material` resource ID 不同；JSON 語意與 byte 長度皆相同。原 native 比較能過只因同一 cache clone 保留相同 HashMap 順序。根因是多個 Serialize 型別以 HashMap/HashSet 迭代順序寫 bincode，ID 隨 hash seed／冷熱 cache 改變。R122 改以排序 serialize_with 修正所有此類欄位，wire format 不變。不得以放寬為語意比較掩蓋：保存與 ID 必須 byte-deterministic。失敗 log `native-single-slot-r119*.log`、`r120.log` 保留。

### R122 native regression runner 在 PowerShell 5.1 誤判
`run_appearance_native_regressions.ps1` 設 Stop 後 `& exe *> log`，Windows PowerShell 5.1 將 native stderr 轉 NativeCommandError 中斷；NativeR122 目錄只有第一個 log，非測試失敗。改為該呼叫期間 Continue，仍以 exit code 與明確 passed count 判定，NativeR122b 全通過。

### R123 C001 legacy provenance 不能以 parity 補標
filename-free 來源萃取與 C001 儲存 snapshot 逐 renderer 比較，全部（含 body/head）資源 ID 不同、部分欄位集合不同（body 26 vs 22 fields），屬不同輸出 pipeline 狀態。不可改用名稱或順序猜 slot；採初始化一次替換整組 accessory 的升級設計。

### R123–R126 geometry dependency 簽章受 editor 執行期環境影響
Context 準備飾品幾何時全部回報 `extraction dependencies changed`。失敗路徑診斷顯示 env_sig 不同：editor 會在執行中切換 `OK_UE_ANIM`、`OK_HEAD_GPUSKIN`、`OK_GPU_SKIN` 等 process 環境，而既有 export 路徑是以 set_var+全域鎖暫設 portable profile。不可在背景 worker 再 set_var（與 live renderer 競態），改為 thread-scoped `profile::portable_export()`；env_sig 與萃取讀取皆經 `profile::var`。測試 fixture 也須用同一 profile 產生，否則仍會不符。

### R129 shaped-globals template 漏重放 renderer 相依步驟
template 先對無 parts globals 套 shape 再插入新 renderer，只補 skin_all，runtime accessory 位置差最多 9.7cm、normal 0.83。非 live apply_shape 尾段會 reset_dynamic_accessories 與 update_dynamic_accessories(0)。補上後 miss/hit/未快取完全一致。parity 測試必須比對「未快取原路徑」，不能只比 hit 與 miss。

### R131 shell heredoc 修補失敗
Bash heredoc 內含特定字元時 Python 修補字串比對失敗，且多個來源檔為 CRLF。改用 Write 工具寫腳本檔，替換前依檔案偵測換行；每次 assert 恰好一處匹配。

## 瀏海陰影層畫成黑塊，額頭變黑（2026-10-17）

C050（Yvonne_shibal）的頭髮是飾品 mod，裡面有一個 `shadow` 網格（386 三角形）蓋在額頭上。卡片的 Material Editor 把它換成 `xukmi/MainItemStudioAlphaPlus`、`_alpha=0.6`：遊戲裡是淡淡的瀏海陰影。我們把它畫成不透明的 `Shader Forge/main_item`（shader_kind 8），額頭在瀏海縫隙間一塊塊變黑。

原因（三層）：
1. `me_shader_registration` 只在 mods 的 zipmod manifest 找得到、而且有可用 UE 程式時才接受替換。OK 卡只帶自己用到的 zipmod，Vanilla+（KKShadersPlus）不在裡面；就算在，Vanilla+ 新家族也被刻意擋下（「要等有自己的 UE 程式」）。
2. 半透明判斷、shader_kind、`_alpha` 只認 Shader Forge 的底線名稱（`main_item_studio_alpha`），xukmi 的駝峰名稱（`MainItemStudioAlphaPlus`）一個都比對不到。`_alpha` 也只對飾品路徑烘進貼圖。
3. OK 卡的 session 快取（`oklib-cache`）的鍵不含擷取程式版本，改了擷取規則也還是讀舊結果。

修正：
- `shader_family()` 把 xukmi 的 `…Plus` 名稱換回它取代的 Shader Forge 名稱；半透明判斷、shader_kind 用它。
- xukmi 的 `main_item_studio(_alpha)` 家族視為已註冊（我們用 Shader Forge 家族的程式畫，不需要 zipmod）。
- `SessionPart::from_mesh` 對還沒烘過的 studio-alpha 材質（xukmi 版，或非飾品角色）把 `_alpha` 乘進貼圖 alpha。
- session 快取鍵加入 `SESSION_CACHE_REVISION`（現在 2）。改到擷取結果時要加一。

驗收：互動場景 2560×1400 近拍，同一姿勢，額頭瀏海縫隙的黑塊消失；log 中部件 `Yvonne_hair` 由 `shader=8` 變 `shader=4`。單元測試 `xukmi_plus_shaders_map_to_their_shader_forge_family` 通過。

另：同一張卡在 KKS 截圖裡頭髮是暗酒紅、上衣是黑；那是 KKS 那邊 KKUTS 著色器的問題（使用者確認），貼圖本身就是粉紅與白，我們的顏色是對的，不要去「修」成暗色。

預防：
- 看到材質名稱比對時，先查 xukmi／其他 mod 的同家族名稱。
- 擷取規則改動後要讓 session 快取失效，否則驗收看到的是舊結果。

## 詳細編輯加物理下拉時的驗收問題（2026-10-18）

- `run_editor_acceptance.ps1` 用 `powershell -File` 執行時，參數預設值中的 `$PSScriptRoot` 是空的，`-Editor` 的預設路徑會變成 `/ok_engine.ps1`。目前的做法是明確傳入 `-Editor E:\PhysX\UE5.8\Engine\Binaries\Win64\UnrealEditor.exe`。
- E: 的 Properties 驗收（C000）在本次修改之前就會停在「Color edits did not change model textures」。驗證方式：還原 appearance 的 Rust 與驗收程式後重新建置再跑（EditorPropertiesBaselineR2），結果相同。所以這不是物理下拉造成的。原因見下一節（同日查明並修正）。新增的兩項 `physics_solver_uses_choice_with_default` 與 `physics_choice_saved_in_card` 都通過（EditorPropertiesPhysicsR1）。
- 預防：驗收失敗時先用相同卡片跑一次「不含這次修改」的基準，確認失敗是否早已存在，再決定要不要修。

## OK 卡「套用並更新角色」後預覽沒換、驗收顏色失敗（2026-10-18）

現象：E: 的 Properties 驗收（C000，OK 卡）停在「Color edits did not change model textures」。修改前（EditorPropertiesBaselineR2）也一樣。

原因有四個，疊在一起：
1. **預覽根本沒重載（產品錯誤）**：OK 卡套用外觀後，輸出是拿掉 OK chunk 的原作卡（`export_edited` 的 `ok_card_source`），由 base library 加 zipmod 開啟。`StartLoad3D` 在獨立卡模式只認 `PortableSourcePath`（okPv、.okpreview 或 okLb），所以顯示「請使用含獨立資源的 PNG」就返回，畫面停在舊角色。run.log 裡套用後沒有第二筆 `OK load3d` 就是這個。
2. **材質編輯器列出所有服裝的條目（產品錯誤）**：MaterialEditor 把每套服裝的衣服（ObjectType 1）、飾品（2）放在同一個清單。詳細編輯全部列出，驗收挑到的第一個顏色屬於服裝 5（`CoordinateIndex 4`），改了在服裝 1 永遠看不到。
3. **驗收只雜湊貼圖**：頭髮、飾品、MaterialEditor 顏色現在是著色器輸入，不再烘進 `_MainTex`（見 memory「Shader colours not in _MainTex」），只比貼圖會漏掉。另外 body 的 SpecularColor 等 MaterialEditor 屬性目前沒有渲染，驗收不能隨便挑第一個顏色。
4. **驗收寫死飾品欄 2**：C000 改成 OK 卡後，飾品欄 2 是空的，髮夾在欄 3。改空欄當然不會動。

修正：
- 新增 `UKKCardWorkspace::LoadableSourcePath`：有 base library 時，原作卡本身也能開啟。預覽載入（`StartLoad3D`、`PortableGallerySignature`、服裝預熱）改用它。圖庫清單仍用 `PortableSourcePath`，不改變列出哪些卡。
- `appearance/properties/plugins.rs`：只列出目前服裝的衣服與飾品 MaterialEditor 條目。保留原本的清單索引，修改仍寫回正確的條目。新增單元測試 `material_editor_rows_of_other_outfits_are_not_offered`。
- 驗收：
  - 貼圖雜湊加入著色器顏色（overcolor、shadow／line／specular、item_colors、eye colors）。
  - MaterialEditor 顏色改挑 ShadowColor。
  - 一般飾品改成自動挑：飾品欄 1（假髮）以外，第一個綁了骨架的欄位。

驗收：EditorPropertiesColorR3，15 項全部 valid=true。report.png 已檢視：假髮在頭上，髮夾移到脖子。

預防：
- 產品流程產生的新卡，要確認預覽真的重載（run.log 要有第二筆 `OK load3d`），不要只看驗收數字。
- MaterialEditor 條目一律依服裝過濾。
- 驗收不要寫死卡片的欄位編號，要從卡片資料挑。

## 原作卡後髮不見：髮型 id 0 被當成空的（2026-10-18）

現象：LOL 資料夾的 Ahri 卡（例如 KK_007604）在一對一場景只有前髮、側髮，長後髮不見。

原因：`lib.rs` 從 `ChaFileHair.parts` 載入頭髮時寫了 `if hid == 0 { continue; }`，把 id 0 當成空欄位。但原作每個髮型類別的 id 0 都是真的髮型，例如後髮 0 是「ロングヘア」。空欄位是目錄裡的「なし」，原本就由 `is_dummy_part` 判斷跳過（延伸髮 id 0 就是這樣跳過）。之前測的卡（C050、C000）頭髮做成飾品，或後髮 id 不是 0，所以沒發現。

修正：
- 拿掉 id 0 的跳過，交給目錄查詢和 `is_dummy_part`。KK_007604 補回後髮 5 個網格（cf_hair_idol_b_00..04），延伸髮 id 0 仍判為空的。
- `SESSION_CACHE_REVISION` 升為 3，`SESSION_PIPELINE_VER` 升為 256，讓舊快取失效。

預防：
- 不要用 id 0 判斷「沒有」。有沒有東西，以目錄項目（なし／dummy）為準。
- 查「缺東西」時，先列出 session 的 parts（role、名稱、頂點數），和卡片的 `hair_ids` 對照，馬上能看出缺哪一類。

## 讀卡器裡一般髮型的頭髮掉在腳邊（2026-10-18）

現象：
- 讀卡器的 T-pose 畫面中，KK_007604、KK_095755 等卡的頭髮全部畫在腳邊，頭是光的。
- 使用者觀察到：同一張卡連續載入兩次就正常。看起來像「先載入別張卡再載入就壞」，其實第一次全新擷取一定會壞。
- 頭髮做成飾品的卡（例如 KK_103512、C050）正常。

原因：
- ChaFileHair 的頭髮由 `AccessoryRuntime::from_hair_source` 建立，掛在臉部骨架的 `cf_J_FaceUp_ty` 下。
- `from_placed_source` 當下只用擺放矩陣建立物理狀態，沒有掛點鏈（`attachment_ids` 為空）。
- 之後 `update_render_impl` 拿到真正的掛點鏈時，`set_attachment_chain_for_step` 因長度不符一律失敗，每次都回傳 None。頭髮頂點一直停在頭部局部座標，也就是畫面原點。
- 第二次載入讀的是開卡快取。物理狀態不進快取，第一次更新會用掛點鏈重建，所以正常。
- 一對一場景用調色盤路徑（`export_skin_stages`），不受影響。
- 頭髮做成飾品時掛在身體骨架上的掛點（`a_n_headside`），不需要掛點鏈，也不受影響。

修正：
- `update_render_impl`：現有狀態的掛點數和目前的掛點鏈不同時，丟掉它，用掛點鏈重建（`AttachedDynamicSet::attachment_len`）。
- `SESSION_CACHE_REVISION` 升為 5（C 的 master 已用掉 4），`SESSION_PIPELINE_VER` 升為 257。
- 測試 `hair_parent_chain_tests::vanilla_hair_is_placed_on_the_head_at_open`。

查找過程：
- 在 Rust 單獨開卡看不出問題，因為只看了 `positions` 這個欄位。
- 最後是逐一檢查 `update_render` 的每個提早返回才找到。
- 讀卡器可以用 `-CharacterMCP -CharacterMCPDir=<mailbox> -CharacterMCPCard=<png>` 啟動，再用信箱請求 `load`（要帶 `session` 與 `revision`）和 `screenshot` 重現。

預防：「重載才正常」通常表示某個只存在於全新建立的狀態有缺陷。要比對「全新建立」與「從快取還原」兩條路徑建出的狀態。
