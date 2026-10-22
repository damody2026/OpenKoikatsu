# 大學城配置錯誤與防範紀錄

## 2026-10-17：使用新版 remesh 強制更新全場景

使用者要求更新 remesh 時，不能因舊模型已有歷史工具證明就跳過。refresh_street_city_remesh.py 從96棟商店、17棟校園、百貨、戶外設施與地形的採用報告收集實際幾何依賴；按實體路徑去重，核心170組另補6組地形材質來源，共176組。每組從原始不可變GLB重新執行指定Rust程式，保留原面數上限、幾何誤差與尺寸接縫設定。新結果先放獨立候選資料夾，核對工具／來源SHA、每個網格命令、雙向曲面誤差、封閉性與面數，全部通過後才原子替換採用檔；備份舊GLB及收據，新舊日誌分開保留。工具或轉接器中途變更須拒絕混用。曲面原先使用的明確來源拓撲恢復與接縫釘選設定保留在新收據，不把外部重網格執行省略成改寫雜湊。

替換後強制重組所有建物，更新材質／地形／戶外依賴證明、完整GLB及Blender，再重新匯入Unreal並驗收；即使幾何輸出SHA相同，採用報告也需引用這次工具的執行證據。最終驗收逐項比較新版工具SHA，不接受只修改舊收據或把歷史驗收當作新版完成。批次進度與舊版備份位於 output/street-social-city-production/remesh-refresh-工具SHA前12碼/。


## 2026-10-17：百貨牆面更新後的城市回歸關閉異常

Unreal Campus 腳本回報 succeeded，且日誌已正常寫到 Exiting／Log file closed，但實際程序退出碼為 -1073741819（0xC0000005），因此交付流程拒收，未以腳本結果取代程序結果。失敗原始日誌／任務／程序資料保存在 mall-v1/failures/campus-shutdown-20261017-215755。重新啟動乾淨的原生驗收程序，重跑城市回歸、百貨唯讀碰撞及最後版本門檻；必須同時取得正常退出與通過報告，不能放寬退出碼或把失敗改成成功。重跑校園原生程序於2026-10-17 22:02:39正常退出（exitCode=0），同時取得通過的檢查報告；首次失敗證據仍保留，後續商店街與百貨驗收繼續重跑。


## 2026-10-17：M1牆面過於單調

- 入口近照仍沿用通用木門材質，與百貨入口不符。主入口依用途改為金屬框與玻璃門扇；室內房門及收貨門分開處理，不可將全部門都套同一材質。

- 第一版材質改版通過幾何檢查，但全景仍像辦公樓：入口辨識不足。二次視覺修正加入兩層門廊、百貨識別及跨層展示看板；圖像用元件局部UV，不能沿用公尺重複UV。看板面須在背板外側，避開原窗與金屬肋，避免被背板遮住或互相共面。

- 立面飾板不得與牆體或樓板外表面共面，否則產生閃爍與假凹凸。外牆改為0.12m基層，外側預留0.08m飾面層，總外框維持150×100m；樓板邊直接分配金屬材質，不再疊加共面的金屬帶。入口門廊正面與基層保持實際深度。屋頂板收進邊緣0.2m，讓壓頂與屋面相接，不得重疊共面。

原模型把外牆、公共牆與衛浴牆一起套成通用白漆，立面只有重複小窗，缺少材質分格、入口層次及玻璃構造。新增獨立 street_city_mall_architecture.py：由用途分配石材／陶板／金屬／衛浴瓷磚，以公尺UV輸出2K底色、粗糙及法線；通用材質函數不得再覆寫已分區的牆。實體窗框、樓層金屬帶、入口門廊及公共柱腳皆複用真實Rust remesh板件。外牆細部留在原外框厚度內，所有門洞、地塊尺寸、通道及每層10萬三角面限制仍用完整匯出模型及Unreal碰撞驗收。先預算再組裝，查驗真實渲染後才交付。


## 2026-10-17：M1 百貨模型

- 補柱網後，服裝層首次超出10萬三角面171面，輸出因此拒收。減少展示架上重複衣物的視覺LOD，保留全部店家、結構與通路；改在 Blender 前解析所有已remesh元件的實際GLB三角面數，連同每個實例、門框、門扇與招牌一起預算，避免完成大半輸出才發現超量。輸出後仍獨立重算，不能以預估代替驗收。

- 大型賣場不能只以少量遠距柱點代表整棟支撐。柱網與商品擺放須用同一座標資料，移除與柱位相交的展示組，並補中庭周邊梁。梁下淨空仍納入完整模型射線驗收；尺寸是遊戲改編，不宣稱結構安全計算或原建物實測。

- 近照檢查發現中庭內手扶梯連接平台缺少側邊護欄，而外圈護欄預留的開口比平台寬，不能只驗收踏階中心線。補齊每段平台兩側、依實際1.2m平台寬縮小邊緣開口；最高樓層沒有上行梯段的位置也需防墜護欄。新增匯出模型與 Unreal 對護欄的實際橫向碰撞檢查。

- 15:29:38 remesh 執行檔於外部更新，原本只比對目前執行檔 SHA 的門檻把全部舊資產拒收。修正為有條件的工具版本沿革：只接受歷史「passed」驗收所雜湊綁定的精確收據／來源／輸出；封存原始驗收與來源文件，沒有證據的新版本仍拒收。未更改舊工具 SHA、舊模型或收據；已測試未知工具 SHA 不能通過。保留此設計以支援使用者已授權的舊物件複用。

- 原生驗收發現北門外1m的地形高達13.1717m，而原地塊基準是12m；不得忽略失敗或把入口接到山坡。改為北側封閉立面、南／東／西入口與東南收貨門。完成面統一高於地形基準60mm，並用已remesh板件剪切生成≤5%入口坡道，避免地形與室內地板重面；保留實際地形探測JSON。
- Unreal Rotator 的字串表示可能含每次不同的記憶體位址，不能拿來驗收既有Actor是否遭修改；保存並比較數字 pitch/yaw/roll、位置、縮放、網格與碰撞設定。

- Unreal 的 ExecutePythonScript 結束後，延遲 Slate 回呼不能再依賴 `__file__`；首次原生驗收在寫報告時觸發 NameError。啟動時保存來源絕對路徑及 SHA，回呼引用固定變數，結束時再核對來源未變。禁止把已存地圖或退出碼0當成驗收成功；需報告與任務狀態一併通過。

- Blender 內建 Python 沒有 Pillow，店招圖像產生不可放在 Blender 裡呼叫；改由主流程的一般 Python 先產生，再匯入封裝。檢查任務狀態JSON而非只看 Blender 的退出碼。店招同時加入實際吊掛支撐，不讓招牌懸空。

- 既有櫃店平面不能直接沿用學校的電梯車廂尺寸。M1 保留150×100m地塊，重建十層80個開放櫃店、四角樓梯、四客梯與兩貨梯；樓板孔與車廂必須使用同一尺寸契約。
- 手扶梯首次增加斜支撐梁時，梁頂侵入部分踏階淨空；保留 `mall-v1/rejected-escalator-headroom.json`，降低支撐梁、增加踏階支架，再重新匯出及驗收。不能只檢查模型存在或包圍盒。
- 公共廁所及管理室門扇須向室內開，避免吃掉公共走道；開放櫃店不放門框與門扇。
- 電梯門檻必須實際連到車廂前緣；一樓也扣除井道樓板，避免車廂底板與樓板重面。另檢查門檻至車廂的逐點承托及站立淨空。
- 150×100m的大賣場不能只放中央幾組家具。依商品類型調整密度，保留2m以上實體通道，並把曲面人台／衛浴面數計入每層10萬三角面上限。來源是已remesh的元件頂點，不能用新方塊冒充remesh資產。

## 2026-10-12：全店研究與 Grok 圖像驗收

- **開放邊必須扣除真正的私密隔牆**：實際 GLB 驗收發現餐廚／試衣後牆仍被當成整面開放邊，6間店失敗。改用共用區間扣除演算法，只宣告真實剩餘開口，逐段再量測結構；不能刪掉廚房或試衣間牆來換取通過。
- **文字雜湊必須對應磁碟原始位元組**：Windows LF／CRLF 不同使提示雜湊誤判。提示寫入後讀檔計算 SHA，產圖前核對；不重新付費生成未變的圖片。
- **圖稿與控制模型分開驗收**：128店逐圖檢查會遇到挑高代替樓板、單跑梯、缺電梯、側牆窗、封閉玻璃前牆或比例錯誤。每張綁定圖像 SHA、具體觀察及退件結論，製作使用完整公尺源資料；不能以標字30m宣稱符合30m。
- **歷史圖不能冒認新版本生成**：舊圖保留原始提示／控制圖／生成收據及來源 SHA。前三間店的新尺寸圖由目前控制模型渲染；歷史 Grok 只引用風格，禁止 import-only 將新控制綁到舊產圖回覆。
- **設備名稱必須與實物用途一致**：便利店補日用品貨架，書店補閱讀桌，烘焙後場補烤箱／工作檯，餐館洗滌獨立列項。不能把藥妝架、電子維修檯或花藝水槽換中文名稱充當其它用途。
- **接觸表必須不可變**：後續圖片完成會改變排頁，接觸表使用內容雜湊資料夾；已審查圖片不能因新圖插入而被其它內容覆蓋。審查同時核對圖像及接觸表 SHA。


- **整層縮小不能直接縮設施**：S18 首次縮至 32×30m，布草設備及廁所核心越界。保留原設施尺度，把進深增至 40m；S19 為 36×36m。每次依同一份樓板、核心、設備外包框重新驗收，不把圖上的建物縮小冒充配置完成。
- **店型比匿名貨架重要**：一般店屋改為按用途選擇 10–14m 面寬、16–24m 進深；餐館保留隔牆熱廚／洗滌、服飾店保留試衣間。展示家具不得因排不下而偷偷縮尺寸，工作設備不可按剩餘面積無限複製。
- **朝北必須轉整棟**：地圖門朝北而室內仍朝南，會使入口、核心與通路斷開。整棟及房間父層統一旋轉 180°；房間局部開口隨父層旋轉，不能再將開口座標轉一次。全店半轉動線測試及開放區連通檢查須通過。
- **重建不得修改進行中的圖像輸入**：Grok 任務依原始資料、提示及影像 SHA 綁定；已啟動任務期間使用 defer-store-review 保持提示不變。新配置不能冒認舊圖通過，過期圖保留待重做。
- **CLI 逾時不代表圖片不存在**：有些 Grok image_gen 已成功，CLI 卻未結束。先按原提示 SHA、單次工具呼叫及工作 session 圖像核對，再匯入既有結果；禁止盲目付費重送。新任務偵測完成的影像收據後給 CLI 30 秒結束時間，只終止自己啟動且已完成影像的程序。
- **收銀必須優先配置**：S13、S33 的陳列填滿前角造成沒有實體收銀檯；先保留結帳與 2m 入口動線，必要時減少可重複的展示件，不能取消工作機能或侵占廚房。

- **陳列數不能按空白面積無限複製**：首版輪流填設備造成香水店出現 7 座洗手檯、花店過多工作檯。已增加各店型設備數上限、把洗烘／摺衣分開，並檢查原有廚房與庫房仍保留。入口朝北的店面不能把收銀放到南側後場，配置依入口方向排列。
- **附屬營業區也要更新**：有後場隔間的附屬區原本被整區略過，造成主客區更新而其餘仍是舊貨架。更新器改為保留封閉後場內的原設備，再更新其外部的店型陳列，不跳過整間。
- **SVG 裝修圖示不能破壞尺寸驗收**：新增旋轉、縮放的家具符號後，原驗收誤將所有裝飾群組當成樓層或門的祖先。共用語意祖先遍歷只讀真正含樓層外框、門及開放邊的鏈；實際樓層的不等比縮放仍須拒絕，已加入回歸測試。
- **Windows 預覽檔案鎖**：產圖器某些位置再次自行 replace，繞過既有的原子寫入重試，造成 R1-F03.svg 存取拒絕。改為共用原子寫入／replace；不刪除原本完整檔，也不把半完成重建列為成功。

- **圖像不是幾何證據**：36 張 Grok 草稿逐張發現單跑梯取代雙跑梯、電梯變廁所、桌椅增加、窗洞改位、睡眠室隔牆消失等差異。逐圖附上影像雜湊與退件原因，保留草稿；正式尺寸依同一份模型重建。禁止把「圖生成成功」當作「模型幾何通過」。
- **設備外包框也要驗收**：四人桌椅造型不能套在 1.1m 深的書桌範圍。渲染前逐頂點檢查，狹窄書桌改為單椅，不能偷偷放大家具占地。
- **研究照片要辨識內容**：自動抓到的圖片可能是 Logo、產品單品、流程圖、健身房或破損 JPEG。必須目視分類，不能把下載數當成有效店內照片數。無尺度的照片只提供用途、造型與陳列參考。
- **不能一套盒子套全部店**：每間店綁定實例及店型需求；花店的水槽／包裝、服飾試衣、單車維修、洗衣排風、餐飲洗滌等不得靠換名稱帶過。全店進度必須逐項記錄，不能以三間完成代表全部完成。

## 2026-10-12：S01／S02／S03 台灣店屋尺度與固定相機重製

- 封閉樓層的門改到左側後，不能仍把中央保留通路畫到實牆上。已補室內 2m 橫向連通、移開 S03 二樓洗滌檯，並以實際結構零件與家具包絡檢查 8,821 次淨空。
- Grok 首張 S01 外觀擅加側牆窗、把入口當吧台並整棟覆木皮，已拒收並保留原圖及理由；改以逐視角、固定無窗側牆與空門洞的提示重製。AI 參考圖不得覆蓋尺寸控制模型。

- **錯誤：把 30×34m 大型通用核心套進普通學生街店屋。** 原每層1020m²（約309坪），功能幾何通過仍不代表店型合理。改用用途驅動的 small_shop_v2，咖啡書屋12×18m、茶店10×16m、烘焙店10×18m；營業前場與公共核心分開列面積。依兩冊 Black House、知冉來日方茶、微物 Flourish Bakery 案例參考用途與尺度；本案尺寸是設計值，並非抄用實測平面。
- **錯誤：店型同質、茶飲沿用商品架、烘焙用教室充數。** 咖啡有書架與閱讀座席；茶飲有點餐／取餐、煮茶、調飲、製冰冷藏；烘焙一樓展售、二樓揉麵整形、發酵冷藏、烤箱排煙、冷卻包裝與洗滌。三樓有獨立睡眠室與淋浴；住宅另有廚房。
- **防範：單一公尺外框。** 地圖、每層SVG、樓板、牆、開口及Grok控制圖由同一份配置編譯。每層完整分配，牆帶另計；2m保留通路、1.2m一般門、2.4m雙扇正門，逐扇檢查開門掃掠與設備／公共走廊。
- **防範：核心不是圖示。** 梯段1.5m、平台1.5m，依層高算踏步；樓梯外框3.8×6.6m。井道3.4×3.2m（S01較寬），另製淨1.8×2.4m可步入轎廂；廁所與洗手逐層存在。原通用6×8m核心不再用於這三間。
- **實作問題：浮點數外框邊界被誤判越界。** 補上1e-6公尺數值容差，保留實際尺寸與邊界檢查，沒有縮放室內或放寬公尺尺寸。
- **實作問題：舊目錄禁止用途名稱更新。** 將新店型用途納入原始規畫及目錄同步，保留互動單元ID；名稱、尺寸、互動與設備同步到CSV和設計工單。
- **實作問題：沒有幾何的語意邊界被當成整間實心方塊。** 明列共享幾何所有者；空的control_components只作資料邊界，組裝器禁止轉成佔位實心殼，實體開口改查共同結構的GLB。
- **實作問題：縮小不同面寬店屋後，舊測試假設每排門中心X完全相等。** 保留相向立面；窄排端部明確規畫休憩廣場，禁止把空白視為有店面。屋間隙仍2m；排內建築覆蓋率按實際店排範圍計算。
- **Grok防範：逐視角生成，再組版。** 前左、後右、俯視各自以固定相機控制圖編修，避免三格請求複製同一角度。舊退件圖片保留；相同退件prompt與控制圖禁止當作可重用快取。AI圖只作外觀參考，尺寸與開口驗收以控制幾何為準。
- **回歸防範：** 新增窄店鋪完整樓板、家具阻路、樓梯放不下、設備越界、井道偏移、用途分隔與門扇撞家具的測試；全城舊配置檢查與新店型專用檢查一起執行。

## 2026-10-11

本紀錄追蹤配置產生器、SVG 與尺寸清單的錯誤。設計檢查通過不等於 Tripo 模型或遊戲通行已完成。

| 錯誤 | 原因 | 通用修正與驗收要求 |
|---|---|---|
| 建築很大，房間只集中在中央 | 外觀輪廓與室內排房使用兩套規則 | 使用同一建築外框分割整層；每層未分配面積必須為零；不得將紅色警告當作修正完成 |
| 百貨、民宿單一房間被拉成數千平方公尺 | 直接縮放房間以填滿建築 | 主房間維持用途合理的尺寸；剩餘空間分割成有名稱、門位、設備與用途的實際分區 |
| 房間組合外緣呈階梯狀 | 只排列不同深度的房間，沒有完整分割樓層 | 樓層外框保持與外觀一致；房間、公共空間、牆體及已標示挑空共同覆蓋完整樓層 |
| 側走廊只有 1.6 公尺 | 只檢查主走廊 | 所有樓層主走廊淨寬 4m、側走廊及支走廊淨寬至少 3m；扣牆厚後驗證 |
| 支援房門前有未交代的間隙 | 支援房與核心帶定位不一致 | 門洞必須直接接到可達的走廊；驗證每一門與走廊的相接及連通關係 |
| 長條剩餘空間被切成無入口房間 | 矩形相減順序造成人工碎片 | 先依障礙邊界掃描、合併同寬矩形，再配置房間；每間必須有可達入口 |
| 尺寸通過，但家具、門扇或樓板仍阻擋通行 | 舊驗收只檢查房間包圍盒 | 分開記錄面積覆蓋、門洞、設備占位、門扇掃掠、角色路徑及模型驗收，不得以其中一項冒充全部通過 |
| SVG 標籤溢出小房間 | 固定字級與單行文字 | 依實體房間尺寸換行、縮放並提供完整 tooltip；圖面必須實際渲染檢查 |
| Windows 開啟的 SVG 偶爾寫入失敗 | 直接覆寫被預覽程序讀取的檔案 | 使用同目錄暫存檔及原子替換；保留原檔直到新檔寫入成功 |
| 組裝 JSON 也被預覽占用而無法覆寫 | 原子替換只套用於 SVG | JSON 與 SVG 共用原子寫入工具；大型 JSON 使用串流寫入及緊湊編碼，避免記憶體倍增 |
| 重建結束時validation.json仍直接覆寫而失敗 | 主產線已採原子寫入，驗證器仍用舊write_text | 驗證器與逐棟稽核JSON也改用共用原子寫入；修正後從失敗步驟繼續，必須重新取得當前雜湊的驗收結果 |
| 舊簡模被誤當作新配置驗收 | 只驗證舊 GLB 存在與房間數 | 每次驗收必須記錄當前配置雜湊；舊簡模不得列入新幾何的通過證據 |
| 產生立面時建築紀錄變成整數 | 中庭邊界的暫存變數覆蓋外層建築變數 | 幾何邊界使用具體名稱，避免重用外層物件變數；完整重建必須執行到最後 |
| 無縫分隔仍出現約 0.014m² 的細縫 | 主房間座標先四捨五入到毫米，附屬區域保留原座標 | 保留求解器完整精度作為唯一資料源；只有顯示文字四捨五入，不先修改幾何座標 |
| 全城 SVG 重建速度隨房間數急劇惡化 | 在迴圈中反覆複製逐漸變大的 SVG 字串 | 圖形片段放入緩衝串列，最後一次合併；完整重建時間也列入驗收 |

## 驗收範圍

SVG 與資料配置可離線完整驗收。Grok 圖片、Tripo 真實模型、實際遊戲碰撞及連續通行必須各自取得實際產物與執行證據，不能由配置表推定成功。

## 浮點方向判斷

180° 旋轉的 `sin` 結果是極小的非零浮點數。用其真假值判斷門朝向，會把南北門誤認為東西門，造成 444 筆誤判。驗收改用軸向閾值（絕對值大於 0.5），保留實際幾何不變；四個方向均納入回歸測試。


## 運動館與參考資料修正（2026-10-12）

| 發現 | 原因 | 修正與防範 |
|---|---|---|
| 用重複暖身廳與觀眾休息廳填滿大樓 | 面積分隔先於運動設施需求，未先查台灣場館實例 | 先記錄場館參考與功能鄰接；運動館用實際多面球場及三座50m池，支援房依更衣、淋浴、廁所、器材與水處理需求設置 |
| 泳池上方看台沒有挑空 | 各樓層獨立規畫，沒有跨樓層空間契約 | 泳池宣告跨1、2F；2F觀眾迴廊同XY並挖樓板孔，設1.2m護欄；上下對位納入測試 |
| 更衣與淋浴器材侵入門側3m通道 | 家具檢查只看是否在房間內 | 實際縮退設備排位；主驗證器和獨立驗收均核對門側淨空，新增所有運動館服務間回歸測試 |
| 將搜尋到索引當成讀過比例圖 | 網頁摘要、圖面及尺寸證據未區分 | 每棟記來源、類型、讀取限制與設計推導；未取得圖面明確保留待核讀狀態，禁止捏造實測尺寸 |
| 泳池支援設施在旁邊，但池畔側牆沒有門洞 | 房間配置與外殼開口契約脫節 | 每座泳池獨立實牆、同層更衣淋浴廁所；新增1.8m池畔側門與明確通路，驗收門洞和服務房數量，不靠文字推定連通 |
| 羽球場只按劃線尺寸塞入十二面 | 忽略台灣場地導引的完整安全範圍及場間走道 | 核對體育署羽球章節後採休閒10×20m包絡，改九面，場間淨通道至少3m；高度與用途級別明列，不冒稱國際競賽認證 |
| 雙梯段中心距與宣告總寬不符 | 梯寬1.5m但中心偏移僅0.75m，沒有預留0.3m中隔 | 中心偏移改0.9m，兩段合計3.3m；驗證梯寬、中心距、中隔與核心總寬一致 |
| 只要幾何通過便宣布全部設計合格 | 驗收範圍混淆 | 幾何、逐棟功能、模型及遊戲通行分開列狀態；本次通過不代表Tripo或遊戲已完成 |

### 前輪實際圖面核讀發現的問題（本輪已修正）

運動館1、2樓已按泳池、觀眾挑空與多面球場重排。全城其他建築仍是待逐層功能審查的草案。抽看M1、H1後，發現主要房間之外仍有過多重複附屬空間；即使外框覆蓋與走廊檢查通過，也不能作為最終製作依據。必須逐棟依實例重新分配主要營業／住宿面積、必要後勤比例及設備，禁止把重複的倉庫或休息廳當作功能已完成。此問題保留在逐棟審查狀態，不能送Tripo後才補。



## 用途配置通用修正與回歸驗收（2026-10-12）

上述M1／H1重複附屬房間問題是前輪缺陷。本輪已改用用途配置器，不再循環填入倉庫與休息廳；當前結果以production/functional-acceptance.md及其配置雜湊為準，保留歷史描述避免把修正前問題遺忘。

| 發現 | 根因 | 修正與防範 |
|---|---|---|
| 百貨八間店只占整層中央極小面積 | 主店面固定12×10m，剩餘空間套用通用附屬模板 | 八個店面沿同一外框均分，合計至少占整層60%；每戶有限後勤，實際展售占主要面積 |
| 飯店與住宅以交誼室填滿，沒有增加真正可住的房間 | 剩餘面積只配沙發與衣櫃 | 實際套房／住宅有分隔臥室、私人馬桶淋浴洗手檯；住宅另有廚房冷藏；逐房驗證設備落在對應隔間內 |
| 工程一館連接翼被大量分配為系辦 | 連接翼借用了最後一個系辦的互動ID，誤把用途一起繼承 | 互動所屬與空間用途分開；連接翼作實際教學實作，教學／研究至少占教學樓用途面積65% |
| 較窄宿舍書桌與衣櫃相撞 | 只檢查設備是否位於房內，未做彼此相交檢查 | 狹窄房型改床尾書桌；增加所有非運動館設備兩兩相交驗收及10.24×12m回歸案例 |
| 廚房、醫療清潔等必要設施因後勤比例上限而被省略 | 可用附屬房間太大，整間作後勤會超額 | 改嵌入有真實隔牆、門與設備的小型支援間；必要設施不可空缺，總後勤面積不超過25% |
| 每個寢室都規畫一間洗衣房 | 混淆獨立住戶設施與共享樓層設施 | 私人衛浴屬住戶，洗衣烘衣屬樓層共享；宿舍每層均明列共享洗烘衣間 |
| 小餐廳／展演室出現零件清單空白 | 家具網格由入口向後排，略過中央走道時剛好略過唯一可用一排 | 從後牆排向入口，驗收每個活動空間至少有可用設備；小房間與大型房間均納入測試 |
| 圖面文字互相壓住、超出房間 | 長名稱、固定文字高度與戶內隔間標籤重疊 | 縮短編號顯示、裁切標籤、按隔間寬度調整字級；完整名稱保留tooltip與搜尋表，實際瀏覽器渲染核讀 |
| 等大店面被錯判不等大 | 77.75與77.74999999999999浮點比較採完全相等 | 幾何保持全精度，驗證採0.00001m顯示精度比較，不為了驗證而改壞座標 |
| 舊Grok示範圖仍被當成本輪已生成參考 | 只檢查舊圖片存在，未對照本次家具配置 | 所有當前工單重新標記未生成；歷史樣張另存，不能送進本輪正式Tripo流程 |

本輪驗收只接受當前配置資料、尺寸與用途，不把尚未生成的Tripo模型、動畫或遊戲導航視為完成。來源不足以證明的施工／設備型號尺寸明列限制。

## 2026-10-12：建地、建築外框與室內淨尺寸混淆

使用者指出室內仍太大，核對後確認主要原因是前輪硬編碼外框過大。牆厚雖已預留，卻用擴大店面及大量附屬房間填滿錯誤外框。前輪用途檢查通過也不能證明尺度合理，因此不可沿用「通過就沒問題」的結論。

| 錯誤 | 通用修正及防再犯 |
|---|---|
| 百貨345×210m，單店約78×74m | 需求驅動外框改96×60m；每層8店；單店面積150–600m²設計範圍納入驗收；百貨block與屋頂邊界相同，停車場在外另列 |
| 宿舍／住宅按大空地擴成數百m² | 主要寢室8×9m、住宅10×12m；按真實床、桌、廚房、衛浴與3m走廊求解；所有附屬住宿也受面積上限檢查 |
| 每間飯店客房再塞布草室與客務室 | 改為每層共用後勤，私人客房保留睡眠、會客、更衣及衛浴；過大附屬客房再分隔 |
| 含牆6×8m核心被誤讀為淨6×8m | 同時輸出gross與clear；扣兩側20cm後5.6×7.6m；廁所設備退至完成面內，洗手檯改到走道端部，驗證不侵入3m通路 |
| 房間入口沒接上戶內3m通道 | 寢室／客房設完整入口橫通道，床桌避讓；新增戶內入口可達檢查 |
| 新增廚房後原有用餐設備全被刪除 | 支援間插入後重查原用途設備；從剩餘可用區重新排位，不能用後場設備數量冒充用餐設施 |
| 小店套用大型雙核心造成外框膨脹 | 小店採單組可步入樓梯／電梯及逐層公共廁所；入口指向實際存在的核心或主走廊，不留不存在的lobbyB |
| 縮放一般住宿影響運動館精確池體 | 運動館尺寸契約優先，SP房間不進一般住宿尺度覆寫；三座50m池和樓層挑空維持對位 |
| Windows預覽占用檔案導致重建中斷 | JSON、SVG、HTML、CSV、JSONL統一暫存寫入後原子替換，暫時共用鎖有界重試；不先刪舊檔 |
| 縮小房間後文字壓住衛浴 | 標籤放在入口預留帶，字級與裁切依圖面調整，小型建築放大圖面比例，實際渲染核讀 |

尺寸資料驗收、實際模型驗收、遊戲導航驗收分開。當前生成工單均標記本輪未生成，歷史Grok樣張不適用新尺寸。完整逐棟外框與牆厚核對見 production/scale-audit.md。

## 2026-10-12：拆件產線及實際控制圖修正

- **教室的用途驗收太寬**：器材櫃或備課桌被計為教學設備，教室主空間可能沒有學生座席。改成獨立教學座位配置及實際座位數驗收；教學備課與器材不再每間教室隔出兩個私有房間，改用系辦／樓層共用設施。3m中心與入口通道保留，普通完整教室至少12個實體設計座位。
- **原始資產工單不是可直接生成零件**：房間外殼、樓板與核心被當成單件。新增共用拆件器：先切真實開口再分最多6m的牆段、樓板及屋頂片；保留公尺座標。原總成禁止直接送Tripo，扶手、機構與專用設備仍有明確待辦狀態。
- **屋頂四捨五入造成超框，中庭使用舊尺寸常數**：改依當前外框及中庭資料扣孔，再裁齊邊片。驗收逐片檢查外界、孔洞與樓板面積，不能只驗總數。
- **樓梯樓板孔偏移**：孔中心原本額外偏東0.75m，與兩梯段不一致。改成與樓梯核心共用中心，保留平台。
- **戶內隔間門位座標系不一致**：原門洞沿用父房間座標，隔牆資產卻以小房間中心為原點。新增資產局部門位，實際牆段驗收每個洞口沒有牆體阻擋。
- **控制三視圖被裁切**：第一筆現行書桌Grok圖沿用裁切控制圖，已退件保留，不送Tripo。控制相機改為投影檢查全部可見幾何角點，四邊至少8%畫面餘量，不再以固定距離猜取景。
- **Blender失敗可能仍回傳0**：Python腳本匯入路徑需明列，渲染引擎需符合當前版本。執行一律加`--python-exit-code 1`，並驗證產物及SHA；不能只看Blender程序離開。
- **Grok允許規則不能用工具ID當前綴**：`image_edit(*)`不是有效的`--allow`前綴。改為工具白名單只開讀檔及影像編輯，禁Agent；不修改使用者設定。CLI真實輸出與退件紀錄一併保留。

### 本輪實際生成／匯出紀錄

- Tripo登入狀態應實際重查，不能沿用舊的「缺少入口」判斷。本輪已接通網頁並生成第一件書桌，模型編號`b5e24e87-057a-43ff-9259-a200fa9c9e70`，H3.1、30點，42,100面。GLB／OBJ正式匯出多次啟動後未收到可下載檔，記為`generated_export_blocked`，不能寫入approved或冒稱已組裝。
- Grok教室三視圖保留了桌椅，但擅自增加講台及背景牆，已退件。房間參考圖只能提供外觀，精確尺寸、物件數與相機須與控制圖逐項對照；不能因為圖片好看就算合格。
- 製作紀錄需獨立持久化；重建全部圖面不得把真實模型狀態清回零。以資產幾何簽章比對舊紀錄，尺寸或開口變更時自動使舊紀錄失效。
- 後勤門原本宣告底部中心，實際放置卻混用門洞中心及鉸鏈位置。統一獨立門扇為底部鉸鏈並扣除半扇寬；通路體積本身不需要模型，禁止生成實心走廊。

### 匯出實體驗收原則

不能以檔案存在代替組裝成功。全城43棟控制模型已直接讀取GLB二進位頂點、節點父子變換及來源SHA，612,677項通過，980個主要室內單元無遺漏。控制模型仍標明非Tripo及未經遊戲導航驗收；正式組裝缺件會停止。大型控制模型建立改用Blender資料API，避免每個方塊都觸發依賴圖更新。


## 2026-10-12：共用資產、真實下載與正式模型門檻

- **同型家具逐房重建工單**：改以用途名稱、尺寸、原點與必要子網格建立穩定共用資產；房間歸屬及擺放仍留在每個實例。不能只用尺寸合併，例如同尺寸床與書架仍不同模型。
- **跨型錄仍有重複**：獨立稽核找到運動／廁所洗手台、城市／運動收納架、城市／廁所馬桶三組相同資產。新增跨型錄合併，遞迴更新實例及巢狀設備參照；不同用途或必要開口不合併。
- **JSON尺寸不能證明模型尺寸**：正式組裝器直接讀GLB二進位頂點及完整父節點變換，驗證公尺尺寸、底部中心／鉸鏈原點。拒絕未解碼壓縮、動畫姿態、外部緩衝、未檢查場景網格或不完整檔案。不能信任accessor宣告的min/max。
- **僅填approved=true容易誤放行**：模型SHA、幾何簽章、實際量測、Tripo來源ID、原始參考圖SHA、持久化生成紀錄與品質驗收檔必須同時一致。測試包含完整有效證據能放行、缺來源或換參考圖必須拒絕；測試假資料不會寫入正式資產庫。
- **程式點擊匯出可能沒有副作用**：重新載入後用新截圖確認真正按鈕位置，實際點擊STL匯出才收到2,105,084bytes檔案。不能沿用舊結論說所有格式都無法下載。GLB及OBJ含貼圖匯出仍未收到檔案；網站沒有顯示原因，不能推定是付費限制。
- **STL沒有方向／材質契約**：下載模型先保留原檔與SHA，經實體渲染確認長寬方向後校正。本例繞Z軸90度，尺寸調整後為0.8×0.6×0.75m，各軸比例修正差異3.57%；首次未校正方向的1.84倍比例差候選保留為無效研究結果。不可把拉伸後尺寸符合誤當原始AI模型精準。通用工具預設各軸比例修正差上限5%，超出即退件；已驗證均勻縮放可用、錯方向大幅拉伸必須失敗。
- **Blender載入檔案後的相對輸出路徑失效**：首次候選渲染誤寫到C:/output。所有輸入／輸出路徑在載入BLEND前轉成絕對路徑，重跑並檢查預期路徑真有三張PNG；不只看exitcode。
- **候選網格不得自動批准**：實際桌面及四腳輪廓已渲染核對，STL無UV／PBR，尚未通過拓撲、材質、子網格及互動碰撞驗收。轉出的candidate.glb保持approved=false，與控制模型及正式資產目錄分開。


## 2026-10-12：世界縮為50%與商業街擴充

- **地圖上把建築集中小區塊，剩餘基地空白**：學生街改為48棟店屋、五排連續街廓；每排沿街建築實牆面寬約89.6%，計屋間2m間距後整排約94–95%。新增店屋為獨立三層建築，不以裝飾屋頂冒充可進入店家。
- **世界縮尺與建築縮尺混用**：世界每軸從2200m改1100m（面積為25%）；SVG保留2繪圖單位／公尺。建築、室內及戶外球場使用真實公尺尺寸，所有世界中心、商場街廓邊界、比例尺均換算，不用把SVG當公尺直接匯入。
- **硬編碼43棟、181層、980室內及1000景會阻止合法擴建**：產線、驗收、瀏覽資料及批次組裝改依實際房間與建築清單計數。本輪74棟、283層、1136室內＋20戶外＝1156主要互動單元；不刪舊房間來湊數。
- **擴大百貨仍用固定600m²租戶上限**：百貨改150×100m，保留十層各八個主要租戶。驗收按標準層面積分配租戶預算，每租戶不超過1/8樓層、合計至少40%，公共動線及後場另列。新增150×100m回歸，檢查均衡八店及至少3m通路。
- **壓縮世界後原室外球場覆到泳館屋頂下**：重新配置標準28×15m籃球活動區及23.77×10.97m網球活動區，另留安全包絡；慢跑道標為約200m休閒環道，不冒稱400m標準場。
- **車站壓縮後占到南側幹道**：獨立街區稽核捕捉TR與道路相交，移到西南交通門戶並移動月台；住宅支路提前接主路，保留車站完整80×50m外框，不縮小室內掩蓋衝突。
- **浮點尺寸直接比較誤報不均衡**：新百貨八格淨尺寸的浮點尾差統一取6位比較；真實尺寸容差仍由獨立幾何驗收控制。
- **新增辦公樓缺研究與用途**：先查閱台灣設計者的辦公室案例，逐棟登錄O01–O03來源；每層辦公、會議、接待／茶水分配，廁所、樓梯、電梯與清潔機電照正常樓層另列。辦公互動不用學籍申請或學生課務冒充。
- **報表變數沿用錯名**：動態計數改版中曾把primary寫成rooms、doc寫成page；修正後必須整條重建通過，不能因前幾個工具成功就宣稱收尾完成。


### 緊湊世界的最後戶外核對

新增辦公樓後，舊野餐節點仍落在O03屋內，舊舞台及營位也可能壓到道路。改為逐一檢查20個24×18m活動包絡，僅允許工程一館真正的露天中庭作外框內例外；同時核對戶外組裝座標與地形JSON。櫻花步道、野餐區、木平台、舞台、營位及中庭節點均重新定位。

只改戶外節點時，不能替全部室內控制模型盲目換SHA。新增保守輸入比較：逐項比對全部室內樓層、實例、資產、拆件配方與部件；舊控制GLB哈希及先前頂點驗收也須一致，才可保留網格並重新做完整頂點／座標驗收。室內位置、尺寸或拆件偏移變更必須重新匯出。新版批次工具自動使用此機制；生成候選或正式Tripo模型不受此控制模型快取批准。

### 總索引尺寸與座標同步

主用途JSON是求解器輸入，不能把其中初始12×10m直接當擴建後百貨店面的最終尺寸。新增sync_street_city_catalog.py，驗證ID／名稱／用途集合一致後，將已求解的淨尺寸同步到HTML、CSV及僅供用途的舊參考工單；HTML不嵌入龐大設備陣列。完整JSON連到production/effective-city-plan.json，該檔建築世界中心亦同步地形，戶外連結回實際地形節點。規畫輸入保持獨立，避免反覆求解時越放越大。

## 2026-10-12：實際網格後處理與宿舍座席缺漏

- **Tripo匯出完成通知不等於取得檔案**：本輪GLB 512px兩次仍未收到實體下載；通知點擊返回首頁，不能算下載成功。保留原始STL與SHA，採真實網格後處理；本機補製PBR明確記錄來源，不冒稱Tripo原始貼圖。
- **真實STL有小型拓撲缺陷**：找到同一桌腳上4條非流形邊，僅合併局部4頂點，最大移動0.521mm。通用工具限定最多2mm、配方本例1mm；超限或修不成封閉網格就退出，不以方塊或整體重建替換。
- **UV／材質縫會被誤判成孔洞**：glTF會在UV、法線與材質邊界拆開頂點。網格稽核只在獨立副本以0.0001mm焊接同位置頂點，再判拓撲；不改來源、不重算法線來隱藏問題。
- **只比三角形包圍盒會誤報淨空**：新增三角形與使用空間盒SAT，加封閉實體內含射線測試；可抓到三頂點皆在盒外卻穿過使用空間的三角面。
- **UV面積固定門檻誤傷微小有效面**：材質切線產生一個微米級有效三角形，UV面積雖低於1e-12但對應3D面積合理。改以UV相對實體面積判坍縮，非放寬真實尺寸或接受零面積UV。
- **每個UV島獨立套木紋形成明顯接縫**：先用物件座標建立連續木紋，再烘焙到標準UV圖集；材質分界先切三角面，避免按面中心分類留下鋸齒。多物件各自複製材質，不能讓後一物件烘焙覆蓋前一物件。
- **位移記錄不能只看新頂點最近舊頂點**：局部合併時舊點到新點最大移動也要記錄。材質切線的新頂點本來就在原三角面上，改用原網格BVH最近面距離並保留合併操作最大位移。
- **宿舍有書桌卻無座椅**：481個寢室共1924張書桌漏配椅子，窄房側邊放法沒有座位餘裕。改為每側橫向上下舖，後側兩套桌椅；每張書桌綁定獨立椅子。以8×9m及四種入口方向回歸，檢查床、桌、椅、浴室、3m走廊互不重疊。
- **靜態模型通過不代表引擎走查通過**：正式資產登錄核對原下載、方向尺寸校正、後處理配方、實際GLB、網格稽核與三張視覺驗收圖SHA；只批准靜態可重用資產。碰撞、LOD效能、導航及全城完成仍各自驗收。
- **未查帳戶就把下載失敗歸因於付費限制**：先查官方說明，再查真實帳戶。官方 https://www.tripo3d.ai/help/features/why-cant-i-download-my-model 說匯出需要合適訂閱，但本帳戶頁實際顯示Pro，不能因此斷言訂閱不足。座椅模型`beae71da-dbbf-4b21-9706-8117f9a31f58`已完成生成（42,998面），STL與GLB匯出有完成通知，仍沒有收到實際檔案；失敗格式、操作與結果逐次寫入generation-ledger.json。不重複花點生成同件、不購買升級、不把通知當作下載證據。
- **摘要只比狀態字串造成批准數仍顯示0**：正式狀態包含來源及材質資訊，不只等於字串approved。進度頁改成直接讀實際GLB與批准證據門檻；生成筆數讀持久化模型ID。舊的當前報告先存acceptance-history，再依同一來源SHA重建；新增verify_street_city_current.py核對規畫、全部控制GLB哈希、實際資產、局部擺放、HTML／CSV尺寸及回歸，不能手填完成數。
- **候選工具只讀家具清單，拆件無法進產線**：製造清單的745種牆片／樓板等資產沒有語意role／name，不能硬套家具欄位或漏讀。新增共同型錄載入器，同時核對製造清單來源SHA，保留原契約並拒絕ID重複。控制圖、方向尺寸校正、後處理、登錄與進度驗收均可讀拆件；第一片0.8×0.2×3.6m牆已實際渲染三視角。無可繪實體的資產必須停止，不能輸出空白三視圖。新增有效拆件、過期來源、重複ID三項回歸。
- **PowerShell行內Python正則引號錯誤**：PowerShell不以反斜線跳脫雙引號，連結核對指令被shell拒絕，未執行。改把HTMLParser連結核對整合到既有驗收工具，直接用檔案程式碼，避免再拼接多層引號；同時核對進度索引的生成／批准數。

## 2026-10-12：逐棟長寬與每層公尺比例尺

- **外框只有乘號文字，不易分辨長寬方向，也無法目測距離**：全部建築逐層總覽及單層SVG都加上東西寬、南北長的含牆尺寸線，每層下方獨立繪製1m、5m、10m比例尺。依每張圖真正的外框矩形推導繪圖單位／公尺，不用固定像素長度，不改建築或室內幾何。工程一館U形標整體外接長寬。
- **漏改產生器會在重建後消失**：尺寸線與比例尺用共同函式產生，已接入規畫產生器及重建流程。現行圖面可獨立刷新，不必改幾何來源或重匯出控制模型；全棟長寬另列building-dimensions.md／json。
- **XML元素誤用字典下標讀屬性**：第一次刷新以element['width']存取而失敗，未寫出該檔；修成element.get('width')。修復後全部74棟、357份SVG與566個樓層圖框成功核對尺寸、1／5／10m比例尺及來源；另光柵化百貨、小店、運動館實際檢視，避免只看程式成功。
- **再次使用跨shell行內Unicode跳脫導致語法錯誤**：更新索引與檢視入口的行內Python指令未通過解析，沒有寫入變更。修正方式是直接修改Python產生器，索引連結由既有尺寸刷新工具產生；本類文字修改一律用檔案程式碼，不再拼接行內替換。

## 2026-10-12：商店街入口相向與室內一致

- **各排店門全部朝南，隔街入口無法相向**：將入口依共用步行街生成，上側朝南、下側朝北；五排中的第二排設前後雙入口，保留48棟密集排列。主入口位置以真實室內3m側通道為基準，不能只旋轉地圖符號。
- **地圖中央假門與組裝東西入口不一致，固定長度步道可能穿進對面建築**：共同契約記錄58個沿街入口的朝向、1.8×2.7m門洞、局部／地圖座標、3m門前通路與對面商店；產生器同步地形、1F圖、門資產及對應立面開口。步道精確止於共用街中心，不再用固定6m長度。
- **獨立檢查需核對真正圖面及資料**：增加入口落在外牆、連接真實側通道、對面有相向商店、路徑不穿任何建築、實際SVG門線／步道與組裝位置／立面工單一致的檢查；未完成實際門扇與立面不能稱為遊戲通行完成。
- **第一版測試手算面寬誤差1m**：十間30m店屋加九個2m間隙總寬318m，第一間入口X為216.2m；改正測試期望，不挪動建築去配合錯誤數字。相向箭頭原先在窄街中央相交成X，縮短箭頭後重新光柵化檢視。
- **檔案寫入工具短暫無法重寫既有檔**：未刪檔、未重設權限；以直接檔案寫入修正後核對內容及Python編譯。避免把工具寫入失敗當成修改成功。

## 2026-10-12：統一門寬、走廊、門表與開啟弧線

- **最新使用者尺寸取代舊值**：一般門淨寬1.2m、店面／建築正門2.4m、室內走廊淨寬2m；大型建築另列8m深開放門廳及其中2m通道。共用尺寸模組驅動配置、門洞、門資產、組裝、圖面與驗收，不能只改標籤。牆厚仍為0.2m，不縮小建築或家具。
- **有門洞資料卻漏畫門符號**：房間、後場隔間、設備間、公共廁所、廁所隔間、樓梯、電梯及建築入口統一產生逐層門表。平開門必須有門扇及90度扇形弧線；2.4m正門為兩扇各1.2m；電梯使用滑門畫法。獨立驗收實際SVG的開口位置、長度、弧線半徑、鉸鏈和方向，不以數量正確替代幾何正確。
- **S28總覽三層上下排列容易被當成整棟長寬**：地圖與各層一直為30×34m，不能為了總覽畫布比例改動樓層外框。加上每層獨立展開說明與同尺度驗證。小店不需要服務後方房間的右側盲走廊併回使用空間。
- **單店北側放大後，南側複本沿用已放大的尺寸**：重建時面寬超出可用範圍而停止。保留配置前尺寸再各自解算兩個半層，避免重複加上側道面積。
- **舊驗收把所有主要房門硬編成1.4m**：新1.2m門洞被誤判有牆堵門。改從資產明確開口讀取寬度；繼續檢查真實牆體不穿入門洞，不刪除檢查。
- **走廊縮小後，教室入口帶仍保留3m驗收包絡**：家具按2m配置而被舊3m帶誤判。入口帶幾何也改為2m，並重新驗收所有家具／通道交集。
- **騰出的面積使部分宿舍超出已記錄房間面積上限**：以直接連同一走廊的獨立房間再分隔，保留合理宿舍與共用用途，不提高面積上限來掩蓋問題。旅館較窄的房間使用可放得下雙人床、沙發及衛浴的實際配置，仍檢查家具、牆與通道交集。
- **飯店2F混合溫泉、運動用途卻套用客房層比例門檻**：用途檢查依原規畫是否以住宿單元為主決定；客房層仍要求住宿面積占比，不把溫泉層硬改客房。
- **新回歸測試用整數精確比較浮點公尺**：21m計算結果為20.999999999999996，改用浮點容差；幾何門檻沒有放寬。
- **Windows檔案萬用字元和多層文字替換失敗**：rg檔案篩選使用-g；一次性遷移腳本已移除，現行重建只由明確函式處理，避免重跑再次替換。不存在的world-terrain.json更正為實際world-terrain-layout.json。

- **參考核心SVG直接覆寫遇到預覽檔案鎖**：改用既有原子寫入工具，保留舊完整檔並置換新檔，不刪檔或改權限。

- **核對門資產時逐筆列印導致輸出過量**：後續只輸出尺寸分布、錯誤總數與最多三筆異常，不列印完整模型工單。

## 2026-10-12：依用途盤點開放空間，移除實體隔牆

- **把所有互動分區當作封閉房間**：展示、接待、用餐、閱覽及候車應採連通開放分區；客房、衛浴、更衣、廚房、後勤、隔音與隔離用途保留隔間。共用用途政策套用74棟全部主要及附屬空間，逐筆記錄尺寸、決定與理由，不再只修S28或S18。
- **只改名稱或SVG顏色不等於開放空間**：同一政策產生實際全高牆段缺口、移除開放分區內門工單，保留外牆及不同店戶界牆。SVG、生成參考工單、零件配方及控制GLB必須一致，並獨立驗收實際GLB開口沒有牆或門楣。
- **S18一樓接待與交誼應連通**：一樓明確改為接待交誼大廳；二、三樓住宿單元仍需隱私隔間。S28展示區與同用途延伸區連通，後場及清潔隔間保留。
- **資產尺寸發布到毫米、開口仍用無限精度**：16.933333m開口超過16.933m實際牆長使編譯拒絕。依實際發布淨尺寸裁切端點，差值上限0.001m；不放寬結構開孔驗收容差。
- **盤點緊湊店屋主走廊時誤用一般建築寬度**：改用共用main_hall實際尺寸，避免手寫減12m而漏算店屋通道。
- **開放區的盤點頁仍硬寫門寬1.2m**：JSON實體門欄改為null，HTML列出開放連通口數；封閉附屬房間讀取door_clear_width_m，不假設不存在的door欄位。
- **修改來源與實際程式碼不吻合**：失敗的原子補丁未造成修改；重讀精確原文後以小範圍補丁修正。查找檔案先用rg --files，避免臆測模組名稱。
- **生成提示被新開放區提示覆蓋而遺失風格與三視圖契約**：保留統一風格、三個透視角度及控制幾何約束，額外列出確切無牆開口；禁止生成器自行補回隔牆。

- **繪圖順序讓開放牆段遮罩蓋住標籤**：活動區標籤延後到所有牆段遮罩完成後才繪製，並驗收每段開口線端點、長度與整層圖面座標。
- **接待廳仍沿用密集服務終端配置**：增加共用reception用途；櫃檯、候位交誼沙發、員工準備及行李保管分開配置。座席分區最多12組，保留集合及排隊地面；以樓層驗收確保至少一處接待櫃檯與候位座席，不再以大量電腦終端填滿。
- **驗收前察覺圖面及接待用途問題，已停止當次控制匯出**：核對精確工作程序與工作區參數後停止本輪Python及Blender程序；修正後從現行來源完整重建全部74棟，不沿用部分舊產物冒充完成。

- **包廂延伸區被公共用途分類誤開放**：八處共讀、聚餐、桌遊包廂的附屬活動區繼承母單元的私人用途，保留隔間。宿舍公共閱覽區不繼承臥室名稱中的「寢室」，避免反向關閉應開放的共用空間。分類重跑會清除舊open_plan，防止改為私人用途後殘留開放狀態。
- **簡明目錄及舊設計摘要沿用初始家具清單**：HTML與design-only briefs同步有效家具數量、開放／封閉決策及確切開口資料；整合驗收逐筆核對，避免主製作工單正確而簡明頁仍顯示舊配置。摘要重跑先移除先前附加說明，不重複累加。

- **從標準輸入執行部分GLB預檢時缺少Tools匯入路徑**：加入明確模組路徑後完成前48棟實際頂點預檢；預檢另存partial報告，不取代要求74棟完整覆蓋與現行SHA的正式驗收。

## 2026-10-12：補齊開放區端到端公共動線驗收

- **原驗收只能證明有開口，尚未證明一路連通主通道**：新增獨立公共動線圖檢查。相鄰開放區必須兩側均有相向開口；有效寬度讀共用走廊尺寸、牆帶讀共用牆厚。由主要通道逐步搜尋，不接受兩個孤立開放區互通或僅接孤立支線作為通過。
- **只看開口最大寬度可能忽略實際相向重疊**：檢查兩側開口重疊至少2m，發布到毫米的尺寸容差限定0.001m；普通1.2m門不能被誤算為2m公共開放連通口。
- **驗收結果要可追溯到每一區**：盤點JSON及MD新增逐區公共動線路徑；整合驗收每次重新執行空間盤點，要求283層完整覆蓋、全部1,121處開放活動分區都有路徑。
- 新增四項回歸：孤立開放區、只有單側開牆、狹窄開口、不連主通道的支線；同時驗證正常連通鏈與旋轉房間。現行配置未發現斷路，因此不改動已驗收建築幾何。

## 2026-10-12：三間學生街店Grok外觀草稿退件

- **把配置驗收通過誤當作營運尺度合理**：S01、S02、S03都沿用30×34m、三層，每層1,020m²（約309坪）。普通外帶手搖飲店不應被當作整棟大型商場。需先按營運用途及前後場、實際可步入核心回推面積，不能只換材質。
- **Grok改變層數**：S01草稿畫成兩層；所有外觀草稿都保留但標記退件，不能送Tripo或宣稱尺寸合格。
- **透視三視圖重複視角**：前左與後右面板近乎重複，未呈現相反立面。後續必須逐面核對主門、側門、後勤入口及窗格，不只檢查三個面板存在。
- **未核定窗表被AI自行補窗**：外觀草稿中的窗格是提案，不是施工或製模依據；應先把房間用途、外牆實際可開窗位置及固定門洞畫進可核對的立面控制。
- **參考台灣實際店型**：[知冉設計・來日方茶](https://knrndesign.co/project/rairai-tea)為10坪茶飲店，區分Order與Pick Up；[大敦設計・盛杯](https://www.dadun-design.com/single-project-24.html)為20坪手搖飲店。這些是營運尺度參考，不能直接複製到含住宿、樓梯、電梯及公共廁所的整棟建築。
- **原外觀控制地坪錯置到入口上方0.18m**：地坪改為頂面對齊樓層Z值，控制圖已重畫；原始世界及已驗收組裝幾何沒有修改。
- **本次保存範圍**：三棟各四組完整控制圖（外觀及三層），共36張控制視角；Grok實際生成三張外觀圖板，全部退件。尚未委託九張室內Grok圖，避免沿用不合理量體繼續生成。製作紀錄保留在generation-ledger.json的building_references。

## 場景圖稿與既有製作紀錄的後續教訓

- Tripo椅子先前已生成且登入有效，但多種匯出操作均未取得實際檔案；原因未確認。不把網頁模型或匯出通知當成下載完成，必須取得檔案及SHA再驗收。
- 圖像生成與圖像編輯的能力不能混用：image_gen不提供本地參考圖輸入；已確認image_edit可用本地圖片。先前工具探測造成無關測試圖，已退件；正式圖必須有完整場景提示詞與真實工具回執。
- Grok可能把百貨挖成中庭、增加辦公樓、將商街合併為長屋頂或增加海岸住宅。必須逐圖核讀，保留退件原因；正式SVG仍維持獨立建物公尺多邊形。

## 2026-10-13：逐店圖稿轉繪 SVG 與完整驗收

- 南側商業街區原被舊程式當成大塊花園，已新增S49–S96兩排實際店屋。北、南合計96棟、四排及兩條相向步行街。入口、外框、每層尺寸及來源共用清單；新增回歸檢查南側範圍及相向門位，移除固定48／128的驗收假設。
- 百貨的陳列候選原從左欄逐個取用，雖無碰撞卻造成整側空蕩。渲染驗收發現後改為分散配置，維持實際設備尺寸與2m通路；80個租戶均檢查前後左右各區有陳列，並加入回歸測試。
- 圖片是轉繪輸入，校正後SVG是輸出。以image-origin-design.json及image-origin-prompt.txt保留原始生成設計，不把新家具座標冒稱為當初生成圖片的輸入。僅在用途、外框、樓層、門與核心契約一致時復用圖片，圖片與SVG分別綁定SHA。
- 封閉百貨租戶曾手工組出不完整門資料，缺owner與opens。現在沿用已驗證門表，再轉成租戶局部座標；不另造第二套門規則。建構失敗就停止後續驗收。
- 小店的南北尺寸文字與設備清單重疊，已右移清單並增加圖幅邊界，另移除重複覆蓋的開放區文字。
- 77份舊圖片缺新的完成旗標，已由原session恢復唯一圖像工具回執並核對提示詞與原圖SHA；不能虛填完成，也不重複生成已有的有效圖片。兩次新任務只回傳提示詞而未呼叫圖像工具，已重試並確認取得真實圖像回執。
- Grok的JSON輸出有時在物件前帶讀圖說明，直接解析全部text會失敗。現在解析結構化物件、驗證全部店號與成功附圖的read_file回執。批次以系統鎖及獨立session輸出檔防止重疊執行。
- Windows換行問題曾有紀錄，本輪仍在新快取程式重犯：read_text會把CRLF正規化成LF，重新encode後的SHA不同。現提示詞版本一律比較原始檔案位元組；僅文字語意比對正規化換行，並加入CRLF快取回歸。場景跨版本復用另要求相同裁切PNG及提示詞。
- SVG對照頁連到說明MD，應先由建構步驟產生說明，再由驗收更新結果；不能先檢查尚未產生的自有輸出連結。
- 驗收程式原本用split截斷「最新自動驗收結果」後的整份MD，會刪除後續錯誤教訓。已改成只更新自動狀態小節，新增兩項回歸確保後續日期章節保留；本輪已恢復重要後續教訓。
- 176店全部實際渲染並核讀11頁縮圖；尺寸、門弧線與完整樓層另驗原生SVG。模型製作依使用者指示暫停，沒有建立新GLB／BLEND，也不把原74棟控制模型當成新增48棟已有模型。

## 最新自動驗收結果

本輪配置驗收通過；舊的浮點方向誤判已修復。實際模型與遊戲通行仍未驗收。


## 2026-10-13 S01 整層 Tripo 建模

- 使用者改為逐層整體生成，每層最多10萬面；不得沿用逐家具生成或把控制模型冒充Tripo成品。
- 面數按三角面計算，網站設定值不是下載模型的實測結果。
- 舊樓層控制參考的全城版本不同：已逐棟核對 S01 small_shop_design 完全相等，並驗證三張輸入圖 SHA，才允許重用；不可只看檔名。
- 檢查門洞、樓梯、電梯井、2m淨通路與牆厚；整層AI可能熔接、封門，必須如實記錄，不得僅憑生成成功驗收。
- 圖片上傳與生成使用最新使用者授權；只解除 S01 三層建模範圍，其餘地圖保持規畫階段。

- S01 F01 87,199面、F02 84,585面為網站顯示值；GLB/STL/FBX匯出均未收到檔案。使用 Everything 查檔名及今日 GLB/STL/FBX/ZIP，尚未找到下載；以既有 S01.svg 命中確認搜尋器正常。不得直接斷言檔案不存在於未索引位置。
- 使用者要求改用 Chrome；瀏覽器工具實測 Chrome unavailable，需 Chrome 連線才可接續既有模型。不可使用未授權替代瀏覽器控制方式或重新生成來解決匯出問題。

- 2026-10-13 Chrome 已連線，實測 F01 資產詳情與工作區、F02 工作區 GLB 匯出仍未收到下載事件。不能把內建瀏覽器視為已證實原因。頁面有 WebSocket CLIENT_DISCONNECTED/SOCKET_ERROR，僅為觀察訊息，不能當作已確定根因。
- Chrome 下載紀錄 chrome://downloads 被瀏覽器安全政策禁止工具存取；不得用替代介面或 CDP 規避，需使用者手動查看。

- 2026-10-13 修正下載誤判：Chrome 實際已下載 S01-F01/F02，各兩份，Downloads 直接查檔後確認。下載事件 timeout 只能表示工具沒有收到事件，不能宣稱網站未下載；重試前必須直接查目標資料夾／使用者可見下載通知。不得將 chrome://downloads 的工具限制誤當一般檔案檢查也被禁止。每層兩份 SHA 相同，只複製一份到專案，保留使用者 Downloads 原檔，不再重複匯出。


## 2026-10-13 樓梯無法接層修正

- Tripo整層匯出為長度1的正規化模型，不能直接以公尺制Z=3.8堆疊；亦不能假設圖生模型保留雙跑樓梯的終點高度。
- 保留原始下載，另製本機尺寸校正及核心重建候選模型，明示來源與局部驗收範圍；不能宣稱修正版完全由Tripo生成。
- 舊通用產生器只扣除梯段長度，未扣中間平台上方樓板，3.8m層高下頭部淨空可能只剩1.6m。已修改stair_slab_opening並測試全部小店配置；樓板洞涵蓋中間平台，入口側平台保留。全城manifest尚未重建，不冒稱已更新全部建築。
- 本次一樓樓梯24階共3.8m，二樓地坪同3.8m。下載模型裁切核心時保留並插值原UV；新增核心材質為本機簡化材質，寫實材質工作尚待處理。
- 實際重新匯入組裝GLB後337項踏面與0.7m人體採樣、2.2m淨高、接層及外框檢查通過；11項小店相關測試與1項全配置開口回歸測試通過。
- 驗收渲染初次因GLB未含World而失敗；已在缺少World時建立檢查環境並重跑成功。不可只憑Blender行程exit0判斷成功，須核對JSON驗收與輸出影像。


## 2026-10-13 S01 寫實重製進行中

- 兩批共六張Grok圖的材質可用，但幾何仍擅自放大樓梯、改隔間或視角。已明確拒絕幾何，不把它們當整層圖生3D建築輸入。改用SVG重建幾何、Grok材質參考、Tripo整層重新貼材質，來源須如實標示。
- 歷史控制模型small_shop_design相同，仍不能用當前全城instance序號直接找舊物件。新增48店後序號改變，曾導致家具細化找不到物件；改讀模型自身來源snapshot映射，再依語意重建真實桌椅細腿及倒圓，不能只看建築尺寸相同。
- 舊透視篩選使用height-.3會刪除樓梯最後幾階與上層平台；修正控制樓層保留完整梯段到height。
- 不同層高的樓板洞必須同時考慮上行與下行梯段：三樓3.4m自身梯段比二樓3.8m短，若只按自身梯長扣洞會封住二樓最後幾階。修正共享stair_slab_opening與入口平台缺口後，三層476項實際GLB射線檢查通過；11項小店測試與2項開口測試通過。
- Blender渲染相對路徑落在C:/output，已改成resolve絕對路徑並把既有剖視圖複製回正確資料夾。
- Chrome開啟file URL權限後，舊連線仍回報未啟用；重載頁面及重建chooser無效，重設CUA連線並重新取得同Chrome後上傳成功。不得一直要求使用者重複開相同設定。
- 整層來源UV有重疊，Tripo匯入取消Use Original UV，改用重新展開；材質設定4K並Remove Lighting。實際下載後仍須重驗幾何、面數、材質，不憑上傳前驗收直接核准輸出。


## 2026-10-13 使用者明確禁止 Texture 路線

- 使用者要求重做Model，我為了固定幾何擅自改成SVG本機模型加Tripo Texture，偏離要求。此路線及其材質結果全部不採用為S01最終建模。
- 後續只能用Tripo Model重新生成整層3D；先驗收Grok參考圖幾何與寫實質感，錯誤模型不得拿去補材質當作重做完成。
- 不再送Texture任務；既有結果留存為失敗歷史，不冒稱Model生成成果。原先上傳前局部樓梯驗收不等於使用者核准該模型。


## 2026-10-13 S01 參考控制圖：版本一致性與家具佔位物
- 錯誤：只驗證 GLB 雜湊卻讀取同名 BLEND，兩者節點 ID 已不同；另外家具代理名稱以 `_CONTROL` 結尾，不能只用 `/` 前綴移除。
- 修正：直接匯入通過雜湊的 GLB，以其來源 manifest 對應節點；同時排除頂層及零件代理。
- 預防：每次 Model 送出前先看實際參考圖；家具仍呈方塊、樓梯被遮蔽則拒絕，不得生成後再用 Texture 掩蓋。

- Model 多圖圖示是 Batch Images to 3D，並非共同多視角。必須讀畫面模式名稱與費用；本次未送出批次。真正多視角要求 Front/Left/Right/Back，不得拿不同透視角隨意填入。
- Grok 圖仍可能改動椅子數、床方向與房間功能。F02/F03 第一版已拒絕，未送 Tripo。輸出相對路徑時先依 session 找既有圖片，不可重生浪費額度。


## 2026-10-13 Model 專用重製驗收狀態
- S01 F01 真正由 Tripo Model 重新生成：e7c58069-e080-458e-af44-cc0626efa7a3，介面 85,258 三角面、51,774 頂點；不是舊模型 Texture。
- 匯出兩次皆出現 Chrome ERR_BLOCKED_BY_CLIENT，檢查 Downloads 無檔案。停止重複匯出，未調低安全設定，也未把遠端預覽當作本機網格驗收。
- F02/F03 Grok 連續三版仍改動電梯、廁所、臥室位置或功能，全部拒絕，未交給 Model。不能只因圖片漂亮就繼續建模。
- 本次最終模型通過數為 0；476 項通過的是本機參考控制樓梯，不能混寫成 Tripo 生成模型通過。


## 2026-10-13 使用者更正：下載後等待與版本核對
- 每次匯出後先等待 10～30 秒，再檢查 Downloads 的實際檔名、檔案大小與 .crdownload 狀態，不能只依網頁錯誤或下載事件逾時判定失敗。
- 重試前先確認是否已有完整檔案，避免重複下載。
- 本次兩次資料夾核對（間隔 10 秒）：舊版 whole-floor 與 realistic-v2 共 5 個 GLB 確實存在；新 Model 的 S01-F01-model-only-v3.glb 尚未找到。不能把舊版已下載誤寫為全部沒有下載。


## 2026-10-13 Tripo 下載攔截診斷
- 針對同一個已生成 Model 測試語意按鈕點擊及直接滑鼠點擊；兩者皆 ERR_BLOCKED_BY_CLIENT。每次等 20 秒後檢查 Downloads，無新 GLB 或 .crdownload。
- 網路事件診斷未提供攔截來源，不能宣稱是某個擴充功能，也不能宣稱已排除所有自動操作因素。
- 現有工具無法操作 Chrome 內部擴充設定頁，需要使用者提供已啟用的攔截/隱私/下載管理擴充名稱，定位規則後再評估精確網域例外。未修改任何安全設定。


## 2026-10-13 下載攔截：排除未證實推論
- 使用者確認沒有廣告、隱私或下載管理擴充。不能再歸咎這類擴充。
- 對已知下載網域 https://tripo-data.rg1.data.tripo3d.com/ 的單次根路徑探測也得到 ERR_BLOCKED_BY_CLIENT，故不只特定 GLB 匯出連結失敗。
- 查閱 OpenAI Docs：Settings > Computer Use > Chrome > Manage 管理網站允許/封鎖清單；此清單仍需使用者查看，尚未確定是原因。官方來源 https://learn.chatgpt.com/docs/chrome-extension 。
- 只讀檢查 .codex/config.toml 與已知 requirements/project config，沒有找到明寫的 browser_use/downloads deny；未擅自新增 allow 或關閉安全保護。


## 2026-10-13 使用者完成下載與 2K 規格
- 找到 C:/Users/Damody/Downloads/library+interior+3d+model.glb，面數 85,258 與新 Model 一致，已封存並記錄 SHA256。不能因預期自訂檔名不存在就斷言沒有模型；需核對網站原始模型名稱與 URL 編碼形式。
- 使用者權限截圖顯示瀏覽、下載預設都允許；不能再推定是封鎖清單或要求隨意增加例外。手動下載正常，自動控制的攔截來源尚未證實。
- 後續貼圖解析度採 2K，已實際修改 Model 設定。此次既有下載實際內嵌 4K，不覆蓋歷史，也不能誤報為 2K。


## 2026-10-13 新 Model 實體驗收與完整外牆修正
- 新 F01 原始 GLB 確實由 Model 生成，85,258 面；按長邊等比校準 18m 後寬 12.4376m，預定上層到達點 raycast=3.9403m（目標3.8m），踏階高度不吻合，未驗收通過。
- 最重要來源問題：把隱藏部分外牆的剖視圖用來生成完整樓層，AI 忠實生成缺牆模型。完整樓層參考只能移除屋頂，保留全部外牆與門窗洞；展示剖視另存，不能當生產輸入。
- 參考家具 WC 佔位方塊被 Grok 誤判；以可辨識的盆體、座圈、水箱和洗手台替換參考幾何，維持原設備包絡尺寸。
- 原模型不覆蓋、不 Texture；完整外牆版本仍只送 Model、2K、每層≤100000面。


## 2026-10-13 通用參考生成器修正與三層 Model
- 細長書架與衣櫃長短軸不可固定當作 X 寬、Y 深；已按長軸建立正面，短軸作深度，避免書架成為長椅。早先把所有誤差歸咎 Grok 不完整，來源控制幾何也有錯。
- 咖啡機與水槽設備總高 .9m 未包含檯面上機器，修正為1.5m，保持 .9m 檯面；已更新通用規畫器，既有 manifest 尚未全城重建，本次明確記錄 S01 設備覆寫。
- 核心牆接合不能保留共面重疊面；外牆厚 .2m。清除舊牆範圍時必須補回牆腳的樓板條，防止走廊落空。本次實際參考 GLB 增加牆厚與樓板連續性檢查，共500項通過。
- Blender 退出碼0不一定代表 Python任務成功；加入狀態檔與例外捕捉。曾抓到冰箱門片超設備包絡0.5mm，已修正。只有狀態 succeeded 加預期輸出、雜湊與驗收才算成功。
- Model 多視角要用同一幾何、相機方向對應 Front/Left/Right/Back；不是四張各自幻想的 Grok 圖，也不是批次模式。三層新Model已送出，2K、100000面上限、Private。
- 圖片來源明示：原始 Grok 圖只採風格方向；實際生產輸入是尺寸鎖定的3D參考渲染，不冒稱原生Grok圖片，不把本機參考GLB冒稱Tripo成品。
- Windows Computer Use @oai/sky 初始化後服務未連接（Trusted RPC service not configured），未控制Windows視窗，也未改安全設定；繼續標準Chrome流程。

## 2026-10-13 — GPT/Grok image provenance and spatial review
- Do not label Blender renders as Grok-generated pictures. Record geometry controls, style references and final bitmap generator separately.
- Grok F02 placed a second toilet inside the planned elevator compartment. GPT comparison v1 corrects this visually, but a realistic picture is not dimensional acceptance.
- GPT comparison: output/street-social-city-production/S01-remodel-model-only-v3/full-envelope/gpt-comparison/F02/gpt-realistic-v1.png; full prompt and bounded review in generation-notes.md beside it.
- Preserve source Grok image. Verify geometry against SVG/GLB rather than assuming any image generator preserves exact dimensions or stair heights.

## 2026-10-13 — GPT F02 image to Tripo Model comparison
- Job: 2de26efe-c176-456e-83ec-cf11f16e06fb. Single-image Model H3.1, 2K, 100000 triangle cap, 30 credits. UI reports 83678 faces and 50365 vertices.
- Four table groups and main partitions broadly retained, but elevator interior became stair-like geometry and bookshelf shelves were simplified. Do not accept this as a usable floor or infer metric precision from appearance.
- GPT corrected the source image's extra toilet, but that did not guarantee correct 3D reconstruction. Record image correctness and model correctness separately.
- Evidence and job ledger: output/street-social-city-production/S01-remodel-model-only-v3/full-envelope/gpt-comparison/F02/. No GLB download or dimensional acceptance completed for this comparison.

## 2026-10-13 — GPT four-view to Tripo experiment
- Actual GPT image_gen Front/Left/Right/Back PNGs uploaded separately into Tripo cube-icon multi-view Model mode, not batch and not Texture workspace. H3.1, 2K, 100000 triangle cap, 30 credits.
- Job 1188da0a-bfb9-422d-91bb-55afbdb6b5e1 completed; UI 84312 faces, 51134 vertices.
- Visual rejection: duplicate stairs at opposite ends, unintended partitions through open seating area, dark floor regions suggesting missing surfaces. Worse layout preservation than single-image GPT test. No dimensional or walkability acceptance.
- Image generation does not guarantee registered multi-view geometry: output framing and local appearance varied. First front image also introduced elevator-like doorway under stairs and was retried; revised image remained ambiguous. Avoid claiming perfect consistency.
- Evidence, prompts, source provenance and job ledger in output/street-social-city-production/S01-remodel-model-only-v3/full-envelope/gpt-multiview/F02/. Preserve failed result for comparison; do not use it as a floor asset or repeatedly spend credits on unchanged input.

## 2026-10-13 — User-directed Front top / Left exterior side
- Follow the requested two-input experiment exactly: GPT top-down in Front, GPT opaque exterior side elevation in Left, Right/Back empty. No additional overhead views.
- Tripo Model H3.1, 2K, 100000 cap,30 credits. Job9f4906c3-c973-4e62-b0d4-764e5042a907; UI89214 faces,51486 vertices.
- Visual result: broad layout better than four-view experiment, without obvious duplicate stairs or new seating partitions. Appears too narrow/long and furniture low/flattened; stair/landing shape uncertain. Do not call this dimensionally correct or playable without GLB verification.
- Save inputs, prompts and evidence under output/street-social-city-production/S01-remodel-model-only-v3/full-envelope/gpt-top-side/F02/.

## 2026-10-13 — Camera angle clarification
- User means about65degrees above horizontal ground, visibly oblique from a diagonal side, rather than a near90degree overhead plan. Specify angle reference explicitly.
- Prior25degree-off-vertical prompt was mathematically equivalent but image stayed too overhead. Editing a flat plan caused camera persistence. Use perspective geometry reference and check visible wall faces, bookshelf front and furniture legs; do not assume requested angle is achieved from prompt alone.
- New native GPT reference saved in output/street-social-city-production/S01-remodel-model-only-v3/full-envelope/gpt-oblique-65deg/F02/gpt-front-oblique.png. Exact generated camera angle unverified.

## 2026-10-13 — Approved oblique Front plus Left side Model test
- Native GPT oblique Front image approved by user, original Left exterior side retained, Right/Back empty. Tripo Model H3.1,2K,100000 triangle cap,30 credits. Job4efd42c9-bc17-4af3-9aca-c7d82cc64305; UI81707 faces,49671 vertices.
- Visible improvement: furniture has legs/height and broad room arrangement retained; no obvious duplicate stairs or new seating partitions. Remaining defects: bookshelf shelves simplified, dark elevator floor region potentially missing surface; stairs-to-next-floor height and exact dimensions not verified. No GLB downloaded for this visual test.
- Evidence/ledger stored under output/street-social-city-production/S01-remodel-model-only-v3/full-envelope/gpt-oblique-65deg/F02/.
- Earlier interrupted25degree submission did not create a new visible asset: fresh assets list still had9f4906c3 latest and5310 credits before this approved submission. Avoid retrying partially executed generation until checking visible jobs and credit balance.

## 2026-10-13 — Modular component reference gallery, no Tripo upload
- Stop whole-floor AI reconstruction experiments for current asset preparation. Deliver individual component references and per-component triangle budgets first; user explicitly requests no Tripo submission yet.
-16 native GPT reference images saved under output/street-social-city-production/S01-modular-components-v1/, with index.html and components.json. Floor slab explicitly included as an independent component.
- Generated whole staircase turned L-shaped; rejected. Use single tread, landing and railing modules, with positions and riser heights controlled in later assembly. Empty bookshelf and book are separate; remove unsolicited plants/vases from shelf reference.
- Repeated assets still count against per-floor triangle budget. Example instance budget83300 triangles, not measured mesh count. Dimensions and triangle caps are future targets, not properties certified by raster images.
- Floor/shaft openings require exact later cutting. Separate moving door panels from lift car; avoid welding interactive parts. Door clear width must be checked after frames and installation gaps.

## 2026-10-13 — Three modular furniture Model tests
- Generated only bookshelf, table, chair with native GPT reference inputs; Tripo H3.1 Model,2K,30credits each,90total. Triangle caps800/1500/1500; actual UI faces552/1170/1282, all below cap.
- Shelf has warped shelves/frame and back-panel texture ripples; table has unwanted triangular webbing near leg joints; chair broadly preserves four legs/back/seat. Do not conflate budget pass with geometry acceptance.
- Native models remain cloud candidates; no GLB download, metric dimensions, collision or independent texture-resolution validation yet. Other13 components not submitted.
- Exact jobs and screenshots in output/street-social-city-production/S01-modular-components-v1/three-component-test.html and three-component-test.json. Main gallery updated to distinguish generated from unsubmitted components.

## 2026-10-13 — Three components Multi-view + fivefold triangle caps
- User requested two images per component and fivefold original caps, not fivefold previous actual output. Correct new caps: bookshelf4000, table7500, chair7500.2K unchanged.
- Front uses original GPT asset image; Left is separately generated GPT side view. Right/Back empty. All3 Model jobs completed,30credits each,90total. Actual UI faces3026/5654/6990, below caps.
- Table unwanted triangular webs reduced; bookshelf warping/back-panel ripples persist; chair broadly complete with residual irregularity. More triangles do not establish metric or topology correctness. Input views are visually reviewed, not metrically registered.
- Two independent settings changed together; do not infer whether quality change came from multiview or increased face count alone. These experimental higher caps are not yet a global floor-budget revision.
- Compare evidence/URLs at output/street-social-city-production/S01-modular-components-v1/multiview-5x/index.html and jobs.json. No GLB download or assembly acceptance completed.

## 2026-10-13 — Bookshelf replaced with exact board assembly
- User chose wooden-board assembly rather than another generated bookshelf. Build9separate boards:2sides,top,bottom,back,4intermediate shelves. Five open bays, clear bay height0.37m. Cabinet1x0.6x2m; structural panels0.025m,back0.012m.
- Actual exported GLB reimport verification passed:9mesh parts,396triangles,dimensions1x0.6x2m. Shared authored2K oak map embedded. Source script Tools/build_street_city_board_bookshelf.py; output board-bookshelf/ under component gallery.
- Do not claim local board geometry is Tripo output. Keep older generation comparisons as history. Procedural wood should remain low contrast so grain does not look like shape deformation.
- Asset geometry checks only; whole-floor placement/collision not tested here.

## 2026-10-13 — Mandatory instant-meshes-rs remesh stage
- User requires every authored, Tripo or reused asset version to pass E:/Paper/instant-meshes-rs remesh before assembly. Already validated identical remeshed versions can be instanced; no repeated remesh per placement. Policy in Docs/StreetSocialCity/production/ASSET_PIPELINE.md.
- Tool drops UV/materials/hierarchy. Added Blender adapter Tools/remesh_street_city_asset.py preserving original file, processing separate objects, transferring source UV/materials and recording commands/hashes.
- Bookcase9thin boards remesh executed, but rejected: bounds shift up toabout10mm, surface coverage error40.5mm vs5mm tolerance, invalid/nonmanifold output also detected. Cleaning invalid faces is recorded and does not make rejected geometry acceptable. Final candidate2879triangles, not adopted. Original396triangleGLB preserved unchanged.
- Do not claim a CLI success means asset acceptance, or original geometry is already remeshed. Gallery marks original bookshelf as awaiting valid remesh. Cloud-only Tripo candidates have not run local remesh yet.

## 2026-10-14 — Chrome recovery and rectangular-panel remesh
- Chrome retry: initial window returned `window is not a usable app window`; refreshed window list contained no Chrome window. Relaunch returned a new window, but activation/state recovery failed with `foreground window did not report a process id`. No Tripo upload/download succeeded in this attempt. Do not claim cloud jobs were downloaded or bypass this through undocumented browser protocols.
- General thin-panel fix: verify source surface matches a solid rectangular panel, condition to an isotropic cube, run the specified Rust remesher in mixed quad mode, restore exact hard planes and metric bounds. Preserve the actual remeshed topology; original assets remain immutable. Curved assets must not use this route.
- Bookshelf remesh now passes: 432 triangles, 9 boards; exported GLB reimport verifies dimensions, welded manifold topology, positive volume, UV/materials and 2K limit. Other 12 basic rectangular component assets also passed. Full floor assembly remains incomplete: chair, toilet and sink pending; basic door hardware and whole-floor walkability remain to verify.
- Review collage is normalized for visibility; component-library.blend and per-asset GLBs retain actual metres. Never interpret the collage scale as assembly placement.

## 2026-10-14 — Chrome debug: correct browser tool verified
- Bundled read-only checks confirm Chrome extension installed/enabled, selected Default profile, native-host manifest and registry correct; browser/chrome/unified-computer-use plugins enabled. Desktop logs show cua_repl server ready, not a startup failure.
- Root mistake in prior troubleshooting: relying on functions ALL_TOOLS inventory missed the directly exposed mcp__cua_repl namespace (configured to omit code-mode/deferred discovery). Repeated @oai/sky Windows browser capture hit URL-enforcement failures and did not diagnose Chrome extension transport.
- Direct mcp__cua_repl.js cua.getState() succeeds; Chrome extension browser id3 discovered. Bound Tripo user tab625117134, navigated to existing chair job9fd86ec3-2866-4bd7-885f-dc3299cf2d69, rendered6990tri model and opened2K GLB export panel successfully. Use this browser binding for subsequent work, not native Windows capture.
- Submitted one export as S01-chair-multiview-7500-source. Verify actual filesystem after10–30seconds before any repeat. Successful browser control does not yet prove download, model remesh or complete floor acceptance.
- Filesystem verification after waiting: C:/Users/Damody/Downloads/S01-chair-multiview-7500-source.glb exists, 4,238,932 bytes. Chrome extension control and GLB download are now verified functional; no security setting changes or native-host repair required. Screenshot: local-components/chrome-restored.png. Chair remesh and whole-floor acceptance remain pending.

## 2026-10-14 — Modular F02 completion repairs
- File uploads initially failed because the active extension did not have file-URL access. User enabled it; extension reconnected with browser id4 (same extension instance). Rebind that same Chrome instance, not native Windows capture. Both two-view Model jobs submitted at2K/private/H3.1,30credits each: toilet fc5d2b00-4552-461c-b109-487646de35a3 (13977 triangles); sink 9c5fb1a3-4ea0-4b59-ae5d-98f9d86a91f9 (7438 triangles). Source GLBs verified in Downloads after waiting.
- Preserve per-corner UVs but weld duplicate positions before OBJ remesh; exported GLB intentionally duplicates UV seam vertices. Source projection must sew coincident vertices again. Dense remesh plus runtime LOD and source-supported hole repair are explicit stages, not evidence that unchanged originals were remeshed.
- Blender crash was caused by caching borrowed IDPropertyArray/Group values and removing their owning imported objects. Copy only owned scalar metadata and retain mesh datablocks; the repaired assembler now exports and renders successfully.
- Actual exported ray tests caught reduced incoming stair headroom under solid step blocks. Replace cumulative solid stair blocks by0.12m treads and separate stringers; preserve24risers,0.158333m rise and1.5m clear flight width. Re-run mandatory remesh for changed structure.
- Open door hinge must sit outside the1.2m clear portal; a leaf centred on the aperture edge reduces actual clear width. Use an outboard0.03m hinge offset and1.196m leaf inside the1.2m opening.
- Calibrate model axes before dimensions: Tripo toilet's long axis is X, not catalog depth Y; chair back faces diagonally. Canonical yaw corrections: toilet -90degrees, chair +45degrees. Bathroom source catalogue had mistaken body/envelope dimensions and omitted faucet/raised-lid height. Correct physical toilet to0.40x0.72x1.00m and vanity to0.60x0.50x1.00m (countertop about0.8m). Record original targets in components.json; floor plate remains12x18m.
- Final geometry/appearance acceptance is still pending while these repair stages run. Do not mark rejected remesh candidates or the draft missing sanitary assets as complete.

## 2026-10-14 — F02 final acceptance and limitations
- Completed chair/toilet/sink downloads and real-scale Blender assembly. Final GLB reimport: 93,711 triangles counting repeats; 1,442 checks passed, including 12×18m bounds,2K images,1.2m door apertures,2m standing corridor volumes,3.8m stair arrivals and2.2m sampled stair headroom. F01 compatibility uses stair-only context; never describe it as a completed F01 floor.
- Rust extraction can lose sanitary undercuts even at higher density. Voxel union fills some concavities rather than restoring them; source voxel cleaning also removed about36mm of toilet detail and was rejected as the production source. Restored normalized raw source before final processing.
- Explicit --recover-source-topology rebuilds sanitary connectivity from the source AFTER executing the specified Rust remesher; positions use its nearest field only within5mm, with at most1mm displacement. Missing features restore source coordinates. Final sanitary bidirectional error about1mm. This is documented reference-topology recovery, NOT pure Rust output topology and NOT an unchanged original passed off as remeshed. Keep both raw Rust OBJ and rejected candidates.
- Export reimport still finds20 open/nonmanifold chair edges and25 toilet edges (toilet source also25); these are visual static meshes, not certified watertight solids. Sink closed. Do not infer watertightness or runtime collision from the geometry acceptance status.
- Removed stale 13/16 and pending-Chrome assertions from current HTML. Early local-components/acceptance.json remains explicitly historical evidence; component-acceptance.json is the consolidated16-asset result.
- Browser file-URL upload permission and browser navigation policy are different. Attempting to open the final local HTML through Chrome automation was rejected because that tool only permits http/https navigation. Do not use a local server, another browser surface or CDP to circumvent that rejection. Deliver normal filesystem links instead. Tripo uploads/downloads on HTTPS already succeeded.
- Final review: output/street-social-city-production/S01-modular-components-v1/assembly-f02/index.html. Game door/lift controllers and character navigation are outside this geometry acceptance; they are not implemented by these assembly scripts.

## 2026-10-14 — Complete S01 building
- Wrote root model3d.md with the actual modular, GPT/Tripo, remesh, recovery, Blender assembly, download and verification workflow. Full building is F01 cafe, F02 reading room, F03 residence; footprint12×18m, floor levels0/3.8/7.6m, roof11m.
- Do not stack the standalone F02 export three times. It duplicates shared slabs, creates multiple cabs and sends the top stair into a sealed roof. Building-mode structure generation uses one slab per level, uncut ground slab, actual incoming stair holes and no outgoing F03 flight; add void-edge guards. Move rail centres outside the tread edge so clear flight width remains1.5m.
- First F01 build reached102,589 triangles after adding a real prep sink. Rejected before delivery. Use12 reused curved chairs plus4 board-built stools, keeping16seats rather than hiding instances from the count. Final floors91,221/94,191/44,467triangles; whole building229,879. The100k limit applies per floor, not the combined three-floor file.
- A solid countertop through an embedded sink closes the basin. Split prep/kitchen cabinets around the real basin footprint before assembly. Add hob, fridge, bedroom/wardrobe, shower, ceiling panels and bathroom grilles; these are dimensioned rectangular constructions, not Tripo claims or furniture bounding-box proxies.
- F03's original inward apartment door swing crossed the specified horizontal route. Move its protected2m route toY[-3.5,-1.5] and study shelf away from that strip, record the adjustment in assembly-report. Top-storey bedroom light also moved off its partition wall.
- glTF ACTIONS/default scene splitting produced separate clips; only one imported clip was active, so cab/upper doors stayed still. Actual export/reimport caught18 failures. Use SCENE with export_anim_scene_split_object=False; one synchronized clip. Doors close before travel and open after arrival. Verify real imported animation at stops1/80/160 and travel40/120; do not manually reposition meshes to satisfy tests.
- Final2,658checks pass on the actual full export and each independently reimported static floorGLB. No copied synthetic F01 context. Whole GLB SHA256 recorded in acceptance.json and checked again before publication. All17new rectangular furniture/equipment versions and3structure versions passed the required Rust remesh; existing curve/board assets reused.
- Keep construction batches sequential and wait for terminal completion before re-running acceptance/publication; a running session with an older succeeded status file is not evidence that the new batch has finished. Status files and source hashes gate the publisher; do not blindly use stale output.
- Full review and downloads: output/street-social-city-production/S01-modular-components-v1/assembly-building/index.html. Full .blend/GLB, three static floorGLBs, exterior, cutaways and actual-geometry top views delivered. Blender animation is prepared; game controllers/navigation/collision integration remain unimplemented. Source open seams and sanitary topology recovery limitations remain explicit.

## 2026-10-14 — S02 profile and functional-space review
- Generalized shared structure, assembly, ray acceptance and plan rendering with explicit building selection. S02 is10×16m, not an independently scaled S01 interior. Derive stair centre, lift threshold/cab and entry from its manifest; use S02-specific equipment and floor用途.
- First fresh-directory batch failed before remesh because log redirection targeted a folder not yet created. Run-BlenderTask now creates its status/log parent before launching Blender. Gate both process status and remesh-report; existing output alone never proves a new batch succeeded.
- Old publisher ignored a --building argument and still reported S01. The S02 geometry/report was independently verified, then publication moved to the profile-aware publisher with exact building/source-path checks. Never treat a successful S01 publication as completion of another building. Updating HTML is explicitly marked pending until its current export passes.
- Initial furniture fit checks missed operation direction: checkout/front controls faced the street/window, and fridge or locker fronts had insufficient approach space. Turn checkout toward the public route, brewing/sealing toward the production aisle, ice maker toward operators and fridges toward free standing space. Move the employee locker and shorten dry racks; preserve2m public/production/cold-storage routes and separately document the1.3m cashier working strip.
- Stronger locker approach test caught the inward bedroom door crossing its standing zone. Flip this reviewed S02 bedroom hinge to the other side; record doorinfo.hinge_side_override rather than pretending the original plan was unchanged. Do not just remove the failed clearance test.
- S02 floors/stairs use a distinct2K matte ceramic finish. Water basins have actual openings, independently tested by rays below the rim. Source inspection found support gaps: extend sink legs into rims, support tea machine body with feet, connect sealer ring to its backing and seat ice-machine grille against the case. Added a source plate-contact graph gate after these repairs. Hanging menu requires actual ceiling rods, not a floating board.
- Original equipment is dimensioned rectangular construction; curves reuse accepted S01 chair/toilet/basin assets. No new Tripo job or credit spend this turn. Equipment references are Taiwan manufacturer pages documented in model3d.md; simplified game geometry must not be advertised as an exact branded product replica.
- Per-instance measured bounds and intended interactions are recorded, with runtime_connected=false. S01 regression runs in a separate S02/regression-S01 report directory; S01's delivered .blend/GLB remain unchanged.
- Derive the private-room lamp from its room bounds, rather than copying a fixed point from another building. Add lamp-versus-partition intersection checks; a ceiling light crossing a bedroom wall must be moved, not hidden only in preview renders.
- Equipment reservation rectangles may include operation space. Do not stretch a fridge body to the whole1.9m-deep reservation: S02 ingredient fridge is0.9m wide,about0.77m physical depth and1.9m high, with separately protected approach space. Actual instance measurements distinguish body size from reserved zones.
- Final S02 export passes2,697checks:107,563triangles total, floors37,007/34,991/35,565; each under100k, exterior10×16m,2Ktextures and real three-storey stairs/single-cab animation.12new equipment versions plus3structure versions passed mandatory remesh; measured instances and planned interactions saved. S01 regression passes2,739checks with its original delivered GLB SHA256 unchanged. Final S02 review: output/street-social-city-production/S02-modular-components-v1/assembly-building/index.html.

## S02 材質尺度修正（2026-10-14）

- 問題：單色牆面與未定義公尺鋪設規格的磁磚，無法只靠提高貼圖解析度得到合理材質。
- 修正：組裝階段以世界公尺座標重設表面 UV；60×60cm 地磚、3mm縫；衛浴核心牆30×60cm壁磚、2mm縫；其餘暖白塗料。地板不假裝已獨立分割出衛浴區，小磚濕區地坪尚未分區。
- 使用原創程序式2K底色、粗糙度及切線法線貼圖，匯出GLB後檢查實際材質連線及UV尺寸。不是產品實測掃描，也不是止滑等級認證。
- 材質處理位於remesh後組裝，不改已通過remesh幾何；不必為平面牆板重新用Tripo產生尺寸不準的模型。複雜設備仍可使用Tripo Model。
- 預防：不能把有貼圖當成材質正確；必須記錄材質用途、實際重複尺寸、填縫尺寸、非金屬參數、法線及粗糙度輸出。不能僅用Blender預覽驗收。

- 商店街批次 S03：C:\portable\OpenKoikatsu\output\street-social-city-production\S03-modular-components-v1\assembly-building\accept_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S18：Special building requires its own layout adapter: S18。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S19：Special building requires its own layout adapter: S19。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S15：C:\portable\OpenKoikatsu\output\street-social-city-production\S15-modular-components-v1\assembly-building\assemble_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S20：C:\portable\OpenKoikatsu\output\street-social-city-production\S20-modular-components-v1\assembly-building\accept_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S22：C:\portable\OpenKoikatsu\output\street-social-city-production\S22-modular-components-v1\assembly-building\accept_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S26：C:\portable\OpenKoikatsu\output\street-social-city-production\S26-modular-components-v1\assembly-building\accept_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S27：C:\portable\OpenKoikatsu\output\street-social-city-production\S27-modular-components-v1\assembly-building\accept_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S28：C:\portable\OpenKoikatsu\output\street-social-city-production\S28-modular-components-v1\assembly-building\accept_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S33：C:\portable\OpenKoikatsu\output\street-social-city-production\S33-modular-components-v1\assembly-building\accept_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S35：C:\portable\OpenKoikatsu\output\street-social-city-production\S35-modular-components-v1\assembly-building\assemble_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S36：C:\portable\OpenKoikatsu\output\street-social-city-production\S36-modular-components-v1\assembly-building\accept_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S38：C:\portable\OpenKoikatsu\output\street-social-city-production\S38-modular-components-v1\assembly-building\accept_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S18：Special building requires its own layout adapter: S18。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S19：Special building requires its own layout adapter: S19。保留候選與日誌，未通過不可宣稱完成。

## 商店街批次錯誤與預防（2026-10-14）

- 下載事件逾時不代表失敗：可頌GLB實際已落地，等候後查Downloads檔頭及大小，不重複生成扣點。
- GPT可頌側圖第一次仍偏正面，拒用；重新要求兩個尖端在投影中重疊的側向輪廓。
- 原無衣人台參考觸發Tripo年齡提示，未替使用者確認年齡，取消該版本；改製作完整穿衣的服飾展示元件。
- 低面數簡化會遺失吉他調音鈕、單車輪輻；原始嘗試超過5mm已拒絕，新增以測距迭代保護薄特徵及極值的LOD，通過後才remesh。
- S03樓梯燈侵入淨高，改置梯段前方平台；房門打開侵入原橫走廊，整體內移2m通路與受影響家具；重匯出驗收。
- 不能假設所有店家入口朝南。S26等北向店家直接套南向核心會造成樓梯門框占通路、外框超0.2m；新增局部契約180度正規化及world_placement朝向，修正候選必須重驗收。
- 單車店重複擺放5~6台14435面單車會超每層10萬面。新增可複用12999面單車及5999面馬桶、4000面洗手台LOD，各自保留<5mm測距與Rust報告；不能省略每個實例的面數。
- S18/S19大型旅宿不適用small_shop_design，需獨立適配；批次應記為未完成而非跳過後宣稱全街完成。

- 商店街批次 S05：C:\portable\OpenKoikatsu\output\street-social-city-production\S05-modular-components-v1\assembly-building\assemble_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S09：C:\portable\OpenKoikatsu\output\street-social-city-production\S09-modular-components-v1\assembly-building\assemble_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S10：C:\portable\OpenKoikatsu\output\street-social-city-production\S10-modular-components-v1\assembly-building\assemble_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 2026-10-14：只核對來源 SHA 不足以確保組裝用的是當次 remesh 輸出，成果總覽也可能仍顯示過期通過。已加入共用強制 remesh 檢查，串接組裝、驗收與發布；新報告記錄輸出 SHA，既有採用版本釘選 SHA。六項拒絕／通過回歸已通過。
- 花束薄花瓣在 4,000 三角面上限下無法保形，候選 5,830 面被阻擋，未放入場景。依複雜度改為 6,000 面上限後重新處理；整層仍保留十萬面限制，不跳過驗收。

- 商店街批次 S14：C:\portable\OpenKoikatsu\output\street-social-city-production\S14-modular-components-v1\assembly-building\assemble_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S15：C:\portable\OpenKoikatsu\output\street-social-city-production\S15-modular-components-v1\assembly-building\accept_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S17：C:\portable\OpenKoikatsu\output\street-social-city-production\S17-modular-components-v1\assembly-building\assemble_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S18：Special building requires its own layout adapter: S18。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S19：Special building requires its own layout adapter: S19。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S20：C:\portable\OpenKoikatsu\output\street-social-city-production\S20-modular-components-v1\assembly-building\accept_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 商店街批次 S22：C:\portable\OpenKoikatsu\output\street-social-city-production\S22-modular-components-v1\assembly-building\accept_street_city_building-status.json。保留候選與日誌，未通過不可宣稱完成。

- 2026-10-14：家具避讓走廊時，只檢查桌面會漏掉伸出的椅子。改成以桌椅所有成員最大侵入量移動整組；保留2m淨寬。曲面單車按保留區長寬轉90度，不縮扁模型。S09已採用6,000面remesh花束，1F12組、2F6組，重新匯入完整驗收通過。

- 大型建築組裝：複製 Blender 匯入物件的全部 IDProperty 後刪除來源物件，會保留已失效的複合屬性引用，導致 Blender EXCEPTION_ACCESS_VIOLATION。只複製必要的字串中繼資料，網格保留 fake_user，變換矩陣實際 copy；不可保存來源 IDProperty 容器引用。

- 大型民宿驗收抓到門位置語意不同：舊清單部分 door instance 的位置是門片左端，牆體 opening 則是門洞中心。不能直接把左端當門框中心；已改成由對應 shell/partitions 的真實門洞計算門框位置、方向與高度。外牆入口依實際東西南北面決定朝向，不能照用與模型前軸相反的舊角度。
- 大型建築不能硬套24階樓梯驗收：S18一樓24階、二樓22階，各層須讀取自己的 riser_count、flight_run_m 和層高。共享樓板已保留到達平台，樓梯不能再疊一片相同平台。
- 牆角不能一概略過：外牆四邊是淨長，需補牆角柱才能封閉。大型結構已補牆角，需重新生成來源、remesh、組裝與完整驗收，舊候選不發布。

- 長批次的發布中繼資料可能同時被其它步驟讀取。JSON／HTML改用同資料夾暫存檔後原子替換，避免讀到半份文件；製作中的盤點只作唯讀版本核對，完整元件採用收據與規畫發布統一放在批次結束後的最終稽核。初始 batch-results.json 是歷史嘗試，最終成果應以 final-audit.json 和 world-acceptance.json 為準。
- S19重複板件多，僅減少曲面元件仍不足以通過每層十萬面。已在指定Rust remesh後移除共平面的冗餘邊，保留精確外形、UV重建與幾何誤差驗收；板件另存 remesh-planar 版本，未覆寫其他建築已採用版本。最終S19三層均低於十萬面，湯池實際踏階與20張新增休息躺椅已驗收通過。

- 診斷程式的縮排與PowerShell內嵌Python引號曾造成語法錯誤，沒有發布候選。新增／修改腳本先執行py_compile；多行流程保存在Tools腳本，避免在shell巢狀插入含雙引號的f-string。
- 本次統一續做入口已支援S18/S19；已驗收且現行尺寸／配置／來源版本未變的模型會直接複用。S09、S18、S19續做測試通過，個別驗收後再合併場景，不能把smoke測試當成全部完成。

- 合併場景總覽不能只用地圖最長邊設定正交相機：斜視角與1800×1000長寬比會裁掉邊緣。改以實際模型包圍盒角點投影到相機平面、按長寬比自動框滿並保留8%邊界；僅修正相機／視角，不重生或修改已驗收GLB幾何。

## 大量場景逐棟匯入效能與輸出隔離

問題：96棟逐棟匯入 Blender 時，場景累積後 dependency graph 更新使單棟耗時明顯上升；程序仍在前進，不能把耗時誤報成損壞，也不能留下未驗收合併檔當完成品。

修正：增加 GLB 資料表合併器，對已驗收來源做完整索引映射與內容去重，再一次匯入 Blender。新舊路徑隔離輸出，只有通過同一套實際合併檔驗收的版本才能採用。幾何二進位與動畫值不重新量化；未知 schema 明確拒絕。總覽相機另外依畫面長寬比及所有實際包圍盒角點計算，避免裁掉街區。

## 商店街本批完成驗收

96／96棟、288層通過。個別1,320,235項、合併GLB20,061項實際驗收；每層最大98,073三角面、貼圖≤2K。前述缺元件、門位置、階數、面數與大型adapter問題已在本批修復；初始失敗紀錄保留，以shop-street-v1/final-audit.json及world-acceptance.json為最終狀態。

## 大地圖地面與 Unreal 組裝（2026-10-15）

- 原遊戲的編輯器啟動被缺少 OKHGShading 編輯器模組擋住。已建立獨立內容型 StreetSocialCity/StreetSocialCity.uproject，使用本機 Unreal 5.8.3 完成場景；不把獨立場景組裝誤稱為 StreetSocial.cmd 遊戲邏輯已接入。
- Tripo 下載事件等待逾時不代表檔案未下載。這批六個 Ground-*-Tripo-v1.glb 都已在 Downloads 找到，檢查 GLB 標頭與完整長度後才複製；沒有重複生成或重複下載。
- Unreal 只 save StaticMesh 不會一起保存匯入的材質與貼圖。已改為保存每個匯入物件、記錄 StreetCityDependencies；重跑必須確認依賴都存在，否則重匯入，不能只信模型 SHA。
- 壓縮 npz 的每次 key 存取都會重新解壓。大型高度場先一次載入 dict，不能在逐點取樣迴圈反覆呼叫 NpzFile.__getitem__。
- 地面底部長瘦扇形三角形會增加 remesh 細分；底面改成與頂面相同的均勻網格，保持封閉邊界。
- 多個封閉道路小薄片在全域焊接後會產生內部重疊面與非流形接點，Rust 自適應細分曾使記憶體超過100GB。已停止該候選，改為每條道路獨立的連續開放表面，入口平台各自保持封閉實心件。G01 七個部分的 remesh 已通過；不採用舊候選。
- Rust 之後的來源拓樸恢復如實記錄，未宣稱純 Rust 原始輸出。新增 pin-source-extrema 只能配合顯式恢復使用，固定來源XY極值頂點的XYZ，確保模組接縫不被場解微移。每塊最終 GLB 仍須重新匯入檢查，不能以來源網格檢查替代。
- 校園底色不能全部當成廣場。校園草地保留，步道與建築周邊才用石材；車道採共用連續標高函數，避免過河時突然跳高。

## 大地形驗收補充

- Blender evaluated_depsgraph_get 屬於 bpy.context，不是 Scene；驗收腳本已修正並重跑。
- G13長陡三角形的單精度BVH曾觀測5.177mm，超過5mm。恢復網格保有相同三角形索引且逐頂點只移動≤1mm，因此改用對應三角形的凸重心位移上界證明，保留BVH原始值，不放寬容差；不同拓樸會拒絕此證明，四項測試通過。
- S18入口實測鋪面抬高25mm。人行鋪面改成入口標高，土層下移40mm，避免地表穿過鋪面。重跑後16塊、3,314項GLB驗收全部通過，包含24條共用邊界與所有店面入口。



## Unreal 地形組裝與實際渲染修正（2026-10-15）

- Commandlet 可匯入資產，但缺完整 ActorFactory；場景擺放與渲染使用 Tools/run_street_city_unreal.ps1 -Stage Assemble 或 -Stage Render 啟動完整 Editor。完成必須核對腳本狀態及實際驗收，不能只信程序 exit 0。
- Unreal Rotator 使用 pitch/yaw/roll 明確命名參數；位置參數受 NativeMake 的 Roll/Pitch/Yaw 順序影響，曾把地面轉成直立。所有位置、世界範圍及實際碰撞重新驗收。
- HitResult 使用 NativeBreak 對應的 to_tuple() 讀取 blocking_hit 與 location；不能假設 Python 有 location 屬性。
- 停用非同步 shader 的 INI 曾造成引擎啟動斷言。維持預設非同步，呼叫 finish_loading_before_screenshot，等待編輯器 post-tick 後才擷取。
- 水岸改成共用全域平滑高度場；水面頂點依四邊形水域種類取固定海面／池面高度，河面沿河取樣，避免岸邊讀到其他水域的預設高度。校園池塘依 SVG 可見繪製順序辨認。
- Windows Python 文字讀寫指定 UTF-8；由 PowerShell 傳遞中文腳本時使用 Unicode 跳脫或直接檔案工具，避免管線編碼破壞文字。


### 材質綁定與離線渲染的驗收缺口

實際 Unreal 畫面發現空材質槽與預設棋盤格，原先「依賴檔案存在」不足以保證綁定有效。匯入快取新增每個 StaticMaterial 槽不可為空、依賴不可為空；舊 S01 資產在原位置 reimport 仍沿用舊設定，改以 MaterialRepair 子目錄重新匯入，不刪除舊資產。匯入明確開啟材質建立並停用名稱複用；保留來源 SHA 與精確碰撞。組裝後清除 Actor 的舊 override_materials，驗收網格實際綁定；完整 RHI 渲染前重新編譯所有實際材質，等待編譯完成再擷取。

非烘焙場景使用 Movable 日光／天光與指定環境 cubemap。Python 延遲擷取使用官方 unreal.EditorPythonScripting.set_keep_python_script_alive(True)；C++ UEditorPythonScriptingLibrary 的 ScriptName 是 EditorPythonScripting，不能直接使用 C++ 類別名稱。每次啟動先寫 launching 狀態，避免沿用舊 succeeded；只有新腳本成功、實際圖檔存在才發布。


### 共用材質的通用解與效率修正

逐一對大型 StaticMesh 呼叫 set_material 會反覆觸發網格重建；逐個重編譯數千份重複材質也很慢。採用來源 glTF PBR 內容辨識：所有渲染參數、貼圖 UV 通道、sampler 與內嵌圖片原始位元組 SHA 都納入雜湊，名稱與 extras 不參與；不以看起來相近或檔名相同作合併依據。名稱正規化只用於對照匯入材質槽，無法證明內容就拒絕。四項測試涵蓋改名、圖片不同、貼圖索引排列及渲染／UV 參數不同。

正式組裝以 Actor 元件 override_materials 一次綁定內容相同的共用材質，保留來源網格幾何。舊 override 清除後才寫入經驗證的新清單；不是一律讓 overrides 為空。缺少來源材質時才批次修改 StaticMaterial 清單一次，避免逐槽重建。此次共用 151 種材質、8,260 個槽，逐槽來源內容證明保存在 unreal-material-bindings.json。保存地圖重新載入後，渲染器再次逐槽核對實際綁定；只編譯場景實際使用的共用材質。


## 本批地面與 Unreal 最終驗收

地面 16／16 塊、每塊 275×275m，覆蓋 1,100×1,100m；六種實際 Tripo Model 地面來源先 Rust remesh、烘焙 2K PBR，再產生可銜接區塊。16塊地面及8份水面版本全部通過指定 Rust，地面含水面總計867,938三角面，單塊最大62,345。地面重新匯入3,318項、Unreal實際場景3,415項通過；96棟／288層全數入場。保存的地圖重新載入後，8,260個場景材質槽全部與來源內容共用證明一致，151種共用材質經完整RHI編譯，實際渲染已確認預設棋盤格消失。

最終狀態以 world-ground-v1/ground-acceptance.json、unreal-acceptance.json、unreal-material-bindings.json、unreal-render.json、delivery.json 為準。舊失敗紀錄保留為歷史，不視為現行狀態。成果頁為 world-ground-v1/index.html；地圖為 StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap，OpenCity.cmd 可啟動獨立內容專案。這是靜態環境組裝，門／電梯控制器與原 StreetSocial.cmd 尚未接入；學校、百貨等其他建物仍是預留位置。


## 2026-10-16 店面室內合理性修正

使用者指出洗手台反向及門框懸在開放區域。盤點原始模型發現：
- 12間店的18個局部封閉空間（6個廚房、12個試衣間）只有門，沒有周圍牆。原建牆程式錯把「主營業空間開放」當成「所有局部房間都不建牆」。修正為主空間維持開放，private_rooms獨立建立四面牆及真實門洞；與外牆／既有活動區隔牆共用時才省略。
- 原牆體略過所有wall_corner造成角落缺口，新增角柱補齊。架構版本升為structure_revision=2；舊快取不得由新版本簽章直接收編，等價複用必須確認來源是現行已驗收版本。
- 實際Tripo洗手台的水龍頭位於局部+Y，該方向是背側；不能只檢查寬深高。S01三樓廚房洗手台應朝室內+Y，旋轉180度；S18/S19共有12個公共洗手台朝牆，改朝使用區；S19另2個洗手台站立區被內開門擋住，依所屬隔間與門片幾何移至後牆，保留原元件。
- 新增實際匯出GLB的門柱／門楣支撐、局部房間四面牆封閉及水槽朝向檢查；新增設備前方0.5m×0.6m、1.8m高站立區盤點。原驗收「門洞沒有阻擋」不能證明「門真的裝在牆上」。
- 保留interior-repair-v1/before的原始GLB與報告，便於比較。所有新牆體仍先用指定Rust remesh；元件只改組裝位置或角度時複用已通過的remesh幾何。
- S02有專用設備配置，不能交給一般retail profile發布器；S01/S02用各自驗收及組裝報告，普通店面才用通用發布器。
- 等價模型搜尋應先比較配置簽章，再對相符候選驗證來源與收據，避免對每棟不相符候選重複雜湊全部資產。
- 設備稽核的最高頂點高度先計算一次，不能在逐頂點條件內重複max形成O(n²)；矩陣逆及物件變換也先快取。


### 擴大到操作面的補充

補齊廚房牆之後，6家餐廳的工作水槽使用面朝新牆；改由所屬封閉廚房的最近牆面決定背向，並重驗前方空間。新增衣櫃、冰箱、馬桶、洗衣／烘衣設備與大型櫃體的操作面稽核。不能假設所有模型的前方都相同：水槽／馬桶以實際最高水龍頭或水箱幾何判斷背向；櫃體以金屬把手的實際位置判斷正面。

發現共用衣櫃原本用cabinet(0.7,1.5)把1.5m當深度，門位於狹窄端且站立面朝牆。改成寬1.5m、深0.7m的真實櫃體，沿東牆旋轉建構，保留約0.7×1.5m的地面方向；衣櫃移至臥室東北側，避開床邊操作區。新衣櫃先執行指定Rust remesh，432三角面；不能只把舊櫃轉向床而造成新的阻擋。S01冰箱也轉向室內。

大建物的11個私用馬桶前方有牆或其他設備，新增四向候選放置解算，要求本體及0.5×0.6m站立區位於自己浴室內、避開其他家具與內開門；找不到合理位置就拒絕，不移到走道湊數。adapter_schema升至4，記錄每筆原／新位置與角度。四項測試涵蓋被擋的使用面、保留已合理配置、內開門及無解拒絕。

Unreal重新載入大型地圖後，不能立即用射線把尚未建立的物理碰撞當成缺牆。實際比對：初始化期間有1,744個未命中；等待資產載入與60個編輯器tick後，只剩18個已確認缺牆房間的36側門柱未命中。室內碰撞驗收必須在這個等待之後執行，不能靠早期未命中宣稱門洞通暢。

最後實體稽核發現8間10×16m店的較短臥室，雖然衣櫃本體沒有穿模，床仍擋住衣櫃前方站立區。共用配置規則新增床／衣櫃使用區相交檢查，將床往室內移，保留0.6m使用深度、0.1m餘裕並維持西側門扇空間。不能只檢查家具本體包圍盒。另修正Unreal腳本InteriorAfter開關須明確宣告為switch，避免驗收模式未啟用。

Unreal門洞稽核另發現測試狀態假設錯誤：樓層靜態GLB是動畫第1格，唯一電梯轎廂停在一樓，二／三樓門應關閉。實際1626個阻擋結果全部位於這些未停靠電梯門；普通門洞皆通過。修正驗收為一般開門檢查暢通，上層電梯門檢查兩門片在三種高度的阻擋，保留門柱支撐檢查。不為通過測試而把沒有轎廂的門打開；引擎動態電梯控制器仍是獨立未接入項目。

#### 2026-10-16 最終驗收完成
96棟／288層已更新並重新匯入、保存Unreal地圖。實際GLB檢查1065個設備使用區、1294個門牆連接均無剩餘問題；來源檢查1513547項、合併模型20061項、初始化後Unreal檢查13466項、交付版本／連結核對2353項全部通過。修正21個水槽位置或朝向、18個缺牆房間、11個馬桶、1台冰箱、8張床；94個衣櫃來源更新（含93個位置調整）。來源檔SHA改變與實際位置／角度改變分開記錄，不能把重新匯出的同姿態元件誤算為朝向修正。
驗收頁：output/street-social-city-production/interior-repair-v1/index.html；逐件紀錄changes.json，總驗收acceptance.json，交付複驗delivery-verification.json。原始GLB與報告保留before目錄。電梯／門的遊戲控制器仍未接入，本次驗收範圍為模型、配置及靜態Unreal場景。


#### 2026-10-16 交付流程複查：防止沿用過期的Unreal驗收
原交付驗證只核對來源GLB與匯入報告，未將引擎碰撞結果綁定到已保存的原生資產，地圖或uasset改動後可能沿用舊報告。新增street_city_unreal_snapshot.py，對地圖、288個樓層uasset、匯入報告及街區配置共291個檔案取SHA256；碰撞稽核前後均須一致，續跑及交付時再次核對。以竄改收據SHA的負向檢查確認會拒絕過期結果。完整流程改呼叫共同finish_street_city_interior_delivery.py，避免正常與中斷續跑各自維護不同的交付門檻。

本次補強已完成：重新執行Unreal InteriorAudit，13466項通過、0剩餘問題，291個原生／配置檔案版本一致；共用交付流程執行成功，2356項最終檔案、連結、合併模型及原生資產驗證全部通過。96棟／288層的最新驗收頁已重新發布。

#### 2026-10-16 全校建模啟動
先查台科大官方校內配置、設計系空間、圖書館樓層、宿舍須知及運動館資料。原遊戲清單為17棟89層，長寬與樓層數標示為遊戲設計，不冒充原校實測；台科大單位跨館使用，不能把一張配置圖誤解成單位只在一棟。新流程保留U形工程館、泳池挑空和專用運動區。樓板在完成面下0.3m，樓層歸屬須用-Fxx識別而非只看Z。舊曲面馬桶強制降到1000面造成32mm細部損失，候選拒絕；改用Tripo Model生成原生簡潔曲面馬桶（1345面、2K），不能只放寬誤差或把失敗候選搬進場景。長方板件以指定Rust處理過的單位板件複用，逐實例記錄裁切尺寸；不以包圍盒替代複雜設備。


## 2026-10-16 校園模組化製作

- 公開台科大資料只支持館舍用途、院系關係及部分設備／床墊尺寸；未公開的整館長寬不得冒充實測。本案17棟89層沿用遊戲公尺地塊，宿舍、運動館與圖書館明確列為改編。
- 動態向外開的正門不能用來量測固定建築長寬；固定牆／板外框另驗，門洞和通行再獨立驗證。
- 樓板在基準面下0.3m，不能單憑物件Z猜樓層。樓層歸屬須取明確Fxx或房間資料。屋頂也必須明確歸屬頂樓，避免錯算首層面數。
- 大型板件逐次切洞會產生大量碎長方形；先按相同厚度與高度合併相鄰共面矩形，再組裝，保持所有洞口和實際占用體積不變。
- 洗烘設備、顯微鏡及飛輪車不能用通用工作檯改名稱代替。洗衣區的摺衣檯、廚房爐台及排煙罩、泳池過濾設備亦須按用途建模。
- 不能因面數超標就接受已破壞把手、踏板或水龍頭的簡化。原生Tripo低面版本與可量測LOD分開留證；LOD必須記錄所有源與結果頂點至最近三角面的抽樣誤差，15mm以上拒絕，外框誤差6mm以上拒絕。這是抽樣量測，不冒稱連續Hausdorff界限。所有採用版本仍須再次經指定Rust程式。
- 冷藏櫃／置物櫃的背面靠牆還不夠，必須保留門洞與門前通行帶。SC首層兩處冰箱貼近門邊已列為共通修正案例。
- 圖像渲染的光能不可隨場地面積放大到過曝；渲染必須檢視實圖，攝影機依可見幾何外框自動取景。
- 生產中腳本改變時，正在製作的建物應拒絕簽核並重跑，不能在結尾把新版原始碼雜湊貼到舊版執行結果上。
- C槽曾滿造成檔案截斷。校園、地形與備份輸出改存E槽並保留C原路徑連結；暫存及Unreal DDC指定E槽。腳本編譯、GLB總長及雜湊必須確認完整，不將零位元檔案當成功。
- 校舍基礎地形須低於樓板，泳池基底須另外挖低；E1開放中庭保留原地面。入口以精確鋪面銜接，地形修改後仍逐塊Rust remesh並驗收接縫。

- 完整校園檢視又揭露：不能用假定的orthographic尺度推算畫面範圍。改以Blender實際投影座標逐一量測建築角點，自動留白並拒絕裁切。
- 窄步道小於地形取樣間距時，光靠raster遮罩會漏掉路下頂點，造成草地與鋪面共面。路床改依實際線段距離及網格鄰域挖低；入口也獨立保留精確支撐。
- 學務處應在學生中心二樓有可辨識的服務辦公空間；行政中心使用「學務聯合服務窗口」避免混淆。資訊工程主系辦在T4，EE另標「跨館專題支援」以區分用途。
- 有效契約雜湊包含完整建物資料，作為契約產生器的實際輸入等價判據。修改另一棟的命名不應推翻未變動的建物；幾何產生器仍逐檔嚴格比對原始碼。此次逐棟比對確認只有EE／AD／SC三棟契約變動，其餘14棟有效資料完全相同。
- Unreal匯入／物理驗收使用NullRHI，圖像渲染才啟動RHI，避免物理工作等待不必要的全套著色器編譯。階段名稱須精確匹配，不能讓InteriorAudit先吃掉InteriorAuditCollision的前綴。
- Tools的原始碼已恢復C槽標準路徑，保留大型編譯快取的E槽連結及歷史原始碼副本。驗收同時核對歷史版本與目前幾何產生器，避免路徑搬移造成假快取命中。


## 2026-10-17 校園交付與通用防錯補強

- 鋪面黑色閃爍不能一概歸因材質。對實際場景以攝影機射線診斷，發現 G10-path-003 與 G10-path-007 在同一高度重疊。新增 street_city_paving_union.py：依完整平面方程分組，凸多邊形扣除已占用鋪面，入口優先；水平與有坡度路段共用規則。區塊邊界維持原公尺座標，重新 Rust remesh、驗證接縫後才能匯入。
- 普通教室與運動空間的規畫資料結構不同；發布系辦清單時須以 functional_programme 或 room_type 判斷用途，不能要求泳池都有教室用的欄位。
- bpy 腳本必須透過 Blender 背景工作包裝器執行，不能直接交給系統 Python。系統 Python 負責編排與檔案驗證。
- 原生場景稽核須綁定當次地形驗收 SHA 與實際執行腳本 SHA，匯入後也更新獨立水面，避免更新地面而遺留舊水面。校園和商店碰撞驗收須指向同一份已保存的地圖。
- verify_street_city_campus_delivery.py 交付閘門逐棟核對有效契約、執行版本、採用元件 Rust 收據、GLB 檔案長度、逐層面數、SVG 長寬／比例尺／核心標示、實際模型預覽及取景、完整場景與原生資產版本。失敗時覆寫為 failed，不能留下舊 passed 收據。


#### 鋪面重疊修正的第二次驗收
先前共平面扣除消除了原本的黑線，但全景檢視又找到兩個黑塊。射線實證 G10／G11 的 path-003 與 path-007 仍重疊；其中非共平面四角鋪面被整片略過，其局部水平三角形沒有參與扣除。改成先把非共平面凸多邊形拆成三角形，再依實際交疊區比較高度（2mm相容範圍），而非依全域平面係數四捨五入分桶。真正不同高度的道路保留；入口仍優先。新增7項水平、正負坡度、近共面、跨層道路及混合高度角點案例，並在耗時整校渲染前先對實際Rust地形執行觀察位置的表面與法線複驗。最後仍重新檢視全景、跑原生碰撞，不以只通過最早三個像素位置視為全圖完成。

- 逐層SVG的牆線依實際高度與樓層剖切面判斷，不能先依牆體起始樓層略過。挑空泳池的跨層牆、合併的高牆仍須在上層平面出現；樓梯踏板則保留明確樓層歸屬。

- SVG樓板也必須依已建模的實際板件繪製，不以整棟外框單一填色替代樓板。露天中庭、樓梯井與挑空區保留缺口；交付時逐層核對實際板件數。


#### Windows交付執行環境補強
Unreal稽核收據為passed且日誌正常關閉，但舊PowerShell啟動器曾判斷失敗。改用直接持有本機Process handle的啟動與等待流程，另存實際退出碼；重跑Campus與InteriorAuditCollision均取得退出碼0、腳本succeeded，並在最終閘門一併核對。不能直接忽略非零退出碼。
Windows系統Python未附IANA tzdata，使用ZoneInfo('Asia/Taipei')造成模型完成後發布中斷。交付時間改用作業系統提供的時區感知本地時間，已實測為+08:00；不必安裝全域套件。finalize_street_city_campus.py --finish-only只續跑發布及完整交付驗證；所有GLB、Rust、圖片、地形、原生資產與碰撞收據仍須通過當前SHA檢查，過期資料一律拒絕，不能藉此跳過驗收。
完整流程入口為python Tools/finalize_street_city_campus.py；更新建物後先執行run_street_city_campus.py，更新地形後先建置、Rust remesh與accept_street_city_ground_chunks.py。有效場景圖像以world-render-receipt.json綁定模型、Blender與渲染器版本，來源未變動才可複用。


<!-- campus-delivery-summary:start -->

### 校園本批完成驗收（2026-10-18）

17棟、89層模組化校園已完成，包含教學／研究、系辦與行政、學生中心、圖書館、國際交流、四棟宿舍及運動館。每棟完整GLB／Blender、89份逐層GLB與附長寬及1／5／10m比例尺SVG已發布。以台科大官方用途、館舍關係和公開設備尺寸為主要依據，整館尺寸、樓層及三座50m泳池是既有遊戲地塊改編，不當成原校實測。

最後重新匯入完整校園檢查994項，16塊地形檢查3,433項，實際Unreal校園碰撞檢查52,340項，既有96店／288層碰撞複驗13,754項，皆通過。完整交付以campus-v1/delivery-acceptance.json的當前SHA與逐項結果為準；舊歷史報告保留，不能視為新版場景驗收。

入口、通道、樓梯銜接、門框支撐和設備方向已按實際匯出模型與原生碰撞驗收。門／電梯動作資料保留在原模型；Unreal中的互動控制器及游泳玩法尚未接入。成果入口：output/street-social-city-production/campus-v1/index.html；獨立Unreal工程仍由OpenCity.cmd開啟。

校內運動館南側新增400m一圈、8道操場，中央90×60m足球場，北側坡道連接校內主步道；保留河東800m一圈操場、4面網球場、4面排球場、兩處學生公園、活動草坪與接橋緩坡／落地支柱。戶外實際Unreal碰撞另通過543項；使用已remesh板件／柱件組裝，曲線跑道單獨Rust處理。詳見campus-v1/outdoor/index.html及同目錄驗收。

<!-- campus-delivery-summary:end -->


## 2026-10-17 戶外場地擴充

- 初案只看平面空地，未先查高度：西側20–107m，不能直接擺放平坦操場。固定流程改為先抽樣高度／水域／道路，再選址；本案改河東擴充地塊並保留既有校舍。
- 大量不連續細網線混合整體地板送Rust，三角提取階段申請64GiB後失敗。原始OBJ與tool.log保留；改四邊形場提取及較小採樣，再以明確來源拓撲恢復保留細節，不可將失敗candidate放進場景。
- Tripo樹木Model任務68d2d231-8a36-47ad-a9aa-8fa28151d93b已送出（3000面／2K／PBR，30點），之後Chrome連線中斷；未取得下載與remesh驗收前不列入採用模型。公園遮蔭先由實際棚架提供。

- 四邊形整體提取仍逾時：不再反覆提高面數或記憶體上限。正式解法是複用實際Rust板件／柱件，單獨處理連續彎曲跑道；adoption.json明確區分組裝與remesh報告。
- 坡道端點射線恰落在浮點邊界，導致有地板卻漏命中：尺寸仍驗證精確端點，射線採端點內側1mm，不能放寬地坪高差。
- 球網高度必須從完成面起算，不能從模型零點計算；場線尺寸按外緣量測，將5cm線寬完全畫入比賽範圍。

- 球場門淨寬必須扣掉圍網柱半徑，不能以柱心距宣稱2.4m；改柱心±1.235m、柱半徑0.035m，驗收掃到內側±1.19m。
- 高架連接步道必須檢查下方實際地形，並設落地支柱，不能只放一片懸空長板。
- 戶外單件預覽用獨立Blender場景載入實際adopted.glb；不能反覆隱藏整座校園重算場景，造成不必要的長時間等待。

- 戶外預覽發現十字步道黑斑：多片板件在交會處共面重疊。以street_city_rectangular_paths.union將所有軸向矩形分成互不重疊的公尺板件格，交會格僅輸出一次；實際匯出模型在6個交會點檢查水平三角形覆蓋數必須為1，不能靠抬高材質或關閉陰影掩飾。

- 完整校園總覽只匯入地形，漏了G12/G15/G16既有河水，造成橋下乾河床假象。整場景組裝依所採地形塊同步匯入已認證水層，獨立water來源紀錄與三角面驗收；Unreal原有NoCollision河水必須保留。

- 最終流程的native函式把for迴圈參數命名args，遮蔽外層CLI設定，導致UnboundLocalError。改用stage_args，CLI設定保持獨立；修正後重新跑完整交付流程。幾何／引擎報告已通過時，只能依目前SHA與已保存資產使用明確reuse-native，不能因流程錯誤放寬驗收。

- 遮蔭棚不能只用四角支柱覆蓋28m寬度。共用pergola生成器依寬度自動分成不超過3m柱距，縱梁按柱距分段，跨向遮蔭構件加深至0.45m，淨高仍3m；分段梁僅端面相接，禁止重疊以免共面黑斑。這是遊戲環境的合理構造表現，不代替實際結構設計。

## 2026-10-17 校內操場不可被校外園區取代

錯誤：只配置河東800m運動園區，遺漏校內日常體育課與集合使用的操場。
修正：運動館南側增加400m八道跑道及90×60m足球場，保留河東800m設施。
預防：校園用途盤點要分別確認「校內步行可達」與外部大型運動園區；根據實際門位置連接路網，不能對無門外牆畫通道。以同一參數推導跑道直道／半徑／量測長度，再驗收匯出模型、基座落地、高差護欄及原生碰撞；不能以示意圖或舊版驗收報告當作完成。

本次補西南側護欄時，因既有建模腳本採單空格層級縮排，新增程式多縮排一格而觸發 IndentationError；已修正並用 py_compile 檢查後重新完整生成。預防：修改緊密縮排的舊腳本先檢查周邊實際層級，執行編譯檢查再啟動昂貴的 Blender 流程；失敗日誌保留，不採用失敗輸出。

- 新版remesh盤點不能只看場景採用GLB：地形材質烘焙仍依賴6個原始地面模型，其舊工具證據不在目前全城幾何清單內。補入城市資產目錄的來源remesh收據，總數由170增至176；更新後重新烘焙，再重建及remesh地形區塊，不能只把材質來源SHA換掉。

- 全批remesh後，S01／S02原手寫預覽頁仍顯示更新前的面數與驗收項數；新增 publish_street_city_refreshed_reviews.py，從目前已通過的GLB驗收報告更新三層及整棟數值，保留原圖文，並驗證頁面數值與真實報告相同。新整合流程發布時一併呼叫，不得只換模型而留下舊統計。

- 設備巡檢腳本的 succeeded 只表示診斷正常執行，不保證 findings 為0。整合與最終驗收都需明確核對96棟、零findings、每個door_check通過，以及診斷來源SHA與目前GLB相同。本批實際檢查已確認零findings／零失敗門洞。

- 更新地形remesh後，除16塊接縫驗收外，必須重新執行 accept_street_city_paving_geometry.py。它的 sources 固定實際G10／G11輸出SHA；不能將舊的 passed 報告留給新版地形。已將這項前置檢查加入新版整合流程。


## 2026-10-18 百貨垂直動線

錯誤：原本只設同向斜率的一對手扶梯，連續上樓必須繞回另一端；四部客梯全擠在北側，無法服務整個150×100m賣場。
修正：參考KONE及Schindler官方交錯連續配置，改四組、每組上下一對。相鄰梯道保持相反實體斜率，上下行方向逐層交替，同端平台換道續行。依4.2m層高推導30度、24階、17.5cm階高，最後一階準確到上一層完成面。中庭兩端至少2.4m轉乘走道。客梯分西北／西南／東北／東南四廳，每廳3部；另有2部貨梯。
預防：大型建築規畫必須先分區配置梯廳，預留排隊、輪椅轉向與穿越走道，再放店舖及家具；不得只增加電梯數字而未改實體井道與樓板孔。各層同軸井道、門向、車廂前後橋板、扶梯接層、轉乘平台、護欄及跨層2.2m淨高均檢查實際GLB與Unreal碰撞。共用 street_city_vertical_transport.py 由層高及梯廳／組數參數推導，不在模型輸出後靠平移修補。

- 擴大中庭時將柱列移至x=±16.5m，與九、十樓南側兩間餐飲廚房的中央門洞同軸，實際GLB驗收抓到12個門洞射線失敗。改柱列x=±18.5m，並讓家具避柱規則跟隨同一柱網；保留 transport-v3-first-audit 全部失敗證據。預防：柱網調整必須同時檢查上下樓層廚房／衛浴等封閉空間門口，不能只檢查中庭與主要走道。

- 兩層手扶梯剖視圖不能把頂層通往畫面外上一層的出發梯體一起顯示，否則雖然全棟模型已驗收接層，預覽仍呈現懸空末端。近景保留二到三樓的完整梯體及三樓到站平台，隱藏三樓往四樓的出發梯體；重渲染並重新綁定交付證據。

- 本次新版手扶梯最終唯讀Unreal驗收完成43,486項、零問題，但退出碼為0xC0000005（-1073741819），仍判定交付未通過。失敗收據與完整日誌保留於 mall-v1/revisions/transport-v3-shutdown-failure；重跑唯讀驗收，必須同時具備task succeeded、零issues及exitCode=0，禁止只看驗收JSON宣稱完成。記錄更新後重新綁定校園／商店街及百貨最終交付報告。


## 2026-10-18 商店街牆面個性化

原問題：大多數小店沿用S02暖白漆，共用材質缺乏業種及店主辨識。改20類用途、96店獨立配色、原創圖像、護牆、抹刀紋、材質接縫及輕微外牆使用痕跡。衛浴保持耐洗表面，住宅層不套營業海報；沿用已remesh幾何，不新增碰撞或改動尺寸。
首輪S01近景發現：每4m重複圖像太密，而且三角形中心落在不同語意分區造成同一平面斜切色塊。改按整面共平面面積中心判定材質，特色牆按實際牆長定位圖像；以實際公尺長寬先畫海報再重採樣進UV，避免文字／圖像被非正方形UV比例拉扁。所有店先做實際GLB與渲染驗收，禁止只看材質樣張就批量交付。

- 抽查S03發現：雖已分材質及配色，多類店仍共用抽象圓弧海報，與「麥穗、菜單、自行車線稿」的風格描述不符。已停下自有批次後補齊20類業種原創圖像，保留中斷進度與日誌；來源SHA改變後舊的風格驗收一律失效，全部依原始Blend備份重做並重新驗收，不能只更新文字描述。

- 逐店材質使整合GLB的獨立mesh由約4千增至18,878，兩次Blender GLB重匯入造成大量重複等待。保留完整GLB重匯入驗收，預覽則使用SHA已綁定的原始已驗收Blend直接組裝，量測世界轉換與外框，保存全部來源SHA；只能停止已確認屬於本工作的預覽程序，不能中止或略過驗收。

- 單純append 96棟的全部資料仍會因資料量與ID複製而逐漸變慢；實測一般載入前五棟由0.44秒增至2.57秒，資料改名未改善；link=True的連結實例前五棟維持約0.27秒。全街預覽使用連結實例及原始可編輯Blend，所有來源SHA、幾何指紋及世界外框必須一致。停止的是本工作預覽程序，完整GLB驗收持續執行。

- 連結來源物件直接讀matrix_world會得到尚未評估的單位矩陣，導致幾何指紋檢查拒絕S01。改在原點建立collection instance，從evaluated_depsgraph_get().object_instances取各實例矩陣，再比對原始指紋；S01實測1785個mesh與原指紋完全一致。通過後才套用全街世界轉換；不得刪掉指紋檢查或把單位矩陣當成模型損壞。

- 只重匯入Unreal StaticMesh資產不足以更新場景牆面：既有Actor可能仍保留舊override_materials陣列。ImportShops改為同時把全部既有288個店面Actor重新綁到目前來源材質，逐slot檢查get_material，保存地圖，並快照確認校園／百貨／戶外Actor未改動；以當前GLB材質身分雜湊去重，不以舊slot順序套白牆。

- Unreal Interchange 對既有 mesh 重新匯入時沿用舊匯入設定，可能只更新網格而不產生新材質。改用來源 SHA 分版資產目錄，重新匯入完整 PBR 依賴後再重綁現場 actor；不得以舊白牆材質填補缺失。

- Unreal 單次匯入205個新樓層後，PNG解碼累積至系統提交記憶體上限而退出3；不得把中途資產當作完整交付。來源SHA分版資產可逐檔續用，重新啟動可釋放解碼暫存；新增每12棟保存地圖與材質綁定檢查點，最終仍須288層實際綁定及正常程序退出。原始錯誤日誌保留於shop-wall-styles-v1/interrupted-import-memory.log。

- 牆面改版雖分成20業種，仍統一採腰牆加兩幅線稿海報，只有配色差異不足以形成店家個性。第二版改由Grok Build逐店獨立生成原創Logo與材質圖，使用不同品牌語言、圖形密度與材料種類，保留每次image_gen來源及提示詞SHA。只輸出提示詞JSON而未呼叫image_gen不算完成，須重新生成；不能把程序化配色標示成Grok生成。

- 第二版材質實際渲染檢查發現：以顏色距離去掉Logo全部淺色像素，會把徽章內原本必要的底色也去掉，花紋牆因此穿透字標。修正為只去除與圖像外緣連通的背景，保留封閉徽章內底色；逐店重做材質並驗證。S18首張Grok Logo漏掉「民」字，且葡式彩磚不符合日式民宿用途，拒絕採用並保留歷史，改成和風藍染重新生成。

- Logo外背景仍變成整張方形底板：Pillow的L模式Image.fromarray可能為唯讀，floodfill內部捕獲寫入例外後直接返回，沒有實際填色。已用小圖與真實S03素材確認原因；遮罩必須copy成可寫影像，並斷言已填外部背景比例大於3%。中止範圍僅本工作已核對PID的模型協調器與Blender子程序，原幾何備份保留，舊烘焙器SHA全部失效並重做。

- 逐張Grok Logo檢查補正：S25「四季鮮果」重字、S86「半糖茶屋」多餘字，重新限制完整店名只出現一次。S82／S83／S85／S93淺色字在粉色水磨石、鋼板或木紋上對比不足，保留歷史並重新生成深色高對比字標。全部96店須先驗原圖與精確生成證據，才啟用模型材質。


## 2026-10-18：建築文字三語與品牌貼圖
- 問題：把繁體店名直接烘進所有語系共用的 Logo／特色牆，會讓簡體和英文模式留下繁體字。通用修正：文字使用穩定 semantic ID，三語由同一 catalogue 產圖；無字 Grok 圖案與 PBR 材質共用。原始有字 Grok 草稿保留生成證據，不再充當三語共用文字材質。
- 問題：Logo 按建物總長定位會落入後方核心或被門洞切斷。修正：以實際公共區牆面三角形覆蓋及五條可視射線決定位置；96 棟原始幾何已逐棟通過位置檢查。
- 問題：Blender 自帶 Python 沒有 Pillow。修正：幾何檢查不讀影像，材質產圖交給明確的 host Python 子程序；禁止依賴碰巧安裝的套件。
- 問題：用 startswith('S') 判定商店會誤把學生活動中心 SC、運動館 SP 當成商店。修正：店號必須 fullmatch S\d+，校園使用明確清單。
- 三語檢查：必須同時驗收完整語系鍵、英文無殘留 CJK、2K上限、相同畫布、文字實際邊界、GLB幾何及動畫不變、Unreal實際材質槽和全域參數0/1/2。只有HTML文案切換不算模型材質切換。


## 2026-10-18：Logo 整合字體必須完整在地化
- 錯誤：只用相似無字圖示配通用字體，或只翻譯 HTML 標題，無法滿足原 Logo 內文字的三語切換。
- 正確做法：以每店原始 Grok 完整 Logo 為參考，產生繁體、簡體、英文三張完整品牌圖。保留原圖案、配色、徽章與專屬字體；大字徽記、小旗、票券、胸章等可讀文字也要檢查。
- Grok image_edit 初稿若有錯字，使用 GPT image_gen 定點修稿並記錄實際來源；不得把 GPT 修稿謊稱為 Grok 原始生成。
- 圖片生成成功不等於字形驗收成功。逐圖比對語系表；簡體特別檢查 鱼／魚、纟／糹、钅／金、选／選、单／單、后／後。英文版不得殘留可讀中文字。OCR 僅輔助，不可取代目視驗收。
- 同一提示只寫當次目標語言，避免把三種目標文字同時寫進修稿提示，造成模型混用。
- 以檔案 SHA 和驗收狀態控制正式輸出，未通過的稿件不得發布為完成品。後續重建招牌必須讀取驗收過的完整圖，禁止回退為圖示加通用字體。
- Unreal Python 的此版本類別為 MaterialLibrary；MaterialParameterCollection 欄位使用 get_editor_property。先做原生材質與 0／1／2 全域語言參數小測，再套用地圖。

### 補充驗收規則
- 繁簡轉換不可只靠 Windows 字串轉換結果；店名中的 舖／铺、後／后 已加入集中覆寫，避免語系表本身帶錯字。
- 若圖像修改仍照抄原繁體字，使用「原字清除 → 以正確字形參考重新繪入」兩階段原生生圖，保留中間底稿。
- 展示頁必須只列 zh-Hant、zh-Hans、en，不能把生成聯絡表的 symbol 草稿誤算成第四個語系。288 個 Logo 圖片路徑逐一檢查；切換程式須實際測試每個圖槽。
- 替換有文字的牆面時不能只換無字牆貼，否則 Logo 會消失。完整三語 Logo 要重新烘入同一個已通過遮擋檢查的實體牆面位置。方形 UV 圖看似拉長是世界長寬換算，驗收必須以公尺映射後的比例為準。
- 模型重新匯入可能清除 Unreal Actor 材質覆寫；不能因 PNG 沒變就沿用舊完成紀錄。以保存地圖 SHA、貼圖 SHA 和獨立重開驗收控制快取；場景變動後重套。
- 驗收腳本須在頁面重新發布之後執行，不能拿發布前頁面測試新資產。
# 校園建築入口館名的本地化覆蓋

- 最終驗收：17 棟／34 面外牆館名／繁體、簡體、英文三語，已在獨立 Unreal 程序重開地圖通過材質來源、實際尺寸、位置、朝向與 0／1／2 切換驗收；原有 387 個樓層的語言材質亦通過回歸。證據：`output/street-social-city-production/localization-v1/campus-signs/acceptance.json`。

- 原生驗收實作錯誤：不要以 Unreal `Transform` 的 `str()` 比較位置；結構包裝字串可能包含不同實例資訊，會將未變動的物件誤報。應保存 Transform 值並直接比較，必要時逐一比較位置、旋轉、縮放數值。初次安裝因這項驗收誤報而未儲存地圖，修正後必須重新安裝及重開驗證。

- 問題：室內門牌已綁定三語，不代表外牆館名也已完成。只對現有已知材質做驗收，會漏掉尚未建立的建築招牌。
- 修正：以校園合約的全部 17 棟與每個實際入口產生獨立清單，共 34 面館名招牌。共用 `building.<ID>` 語意及 `MPC_CityLanguage.LanguageIndex`；繁體、簡體、英文各有完整貼圖。
- 預防：入口數必須等於招牌配置數；板件必須有可驗證 Rust remesh 收據；牌面長寬遵循貼圖比例，底部高於門洞，正面朝室外，厚度不與牆面共面。每次重建後需執行原生安裝及獨立重開驗收，不能以圖庫或室內門牌通過替代。

# 百貨專櫃精品化：用途與風格不能只換漆色

- 陳列密度驗收：只按櫃位輪流加一件會讓珠寶、花束等高面數元件耗盡樓層預算，香水仍只有幾瓶。先按實際三角面分配逐櫃預算，再由同源低面數陳列版補足適合大量販售的商品；精細商品與低面數版都要經真正 Rust remesh，不能以更少商品假稱已滿架。
- glTF 通常沿 UV／法線邊界分裂頂點。LOD 前以 1e-6m 焊接相同位置並保留 loop UV，否則 Collapse 會刪掉孤立三角形成破洞。逐件比對原始輪廓誤差，不能為了通過而持續放寬；吊掛服裝若無安全減面結果，就保留精細版。摺衣需符合販售用途，不可無差別放進禮服、內衣或量身西服櫃。
- 手工平板摺衣原型的布料與衣領不夠自然，已棄用，改由 Tripo Model 製作完整摺衣。不要把只有白色領片的薄長方體當成完整布料商品。具有局部原點平移的元件若需縮放，必須縮放整個實例變換，不能只縮頂點而留下原點高度造成懸浮。

- 商品陳列流程修正：來源模型有一雙高跟鞋時，不能使用單隻鞋的 9cm 寬度壓扁整雙；需要以實際內容更新 20cm 整雙寬度，並讓正規化快取同時比對目標尺寸。上衣寬度依成人肩寬與袖寬設定，不能只記身片寬。
- 細長支撐不能僅靠 AI 模型或「nearest surface <5mm」通過：3mm 以內的缺桿仍可能被數值容差接受，實際卻看似懸浮。立燈改以已認證圓柱補足 30mm 連續支柱；修補部件必須計入面數、在單品與場景預覽同時呈現，並對實際模型射線量測桿徑。
- 商品新增後，原本只朝背牆的相機可能把陳列島全部放到畫面外。必須重新選擇能看見店內陳列的空間視角，再提供實際組裝商品近景，不能因「模型中有商品」就接受空的預覽圖。
- 合約存入 JSON 前，樓層字典鍵一律字串化，否則 Python 整數鍵與 JSON 字串鍵會導致相同幾何被誤判不同。模型製作前要先看完新生成的單品圖，不能在尚未檢視工具回傳圖片的同一次呼叫就啟動相依組裝。

- 商品缺漏（2026-10-18）：完成高級牆面與展示家具，不代表店面已有可辨認的商品。原先彩色小方塊、平板衣物及各櫃通用電腦不能作為商品驗收。每櫃須有對應用途的商品清單、真實公尺尺寸、來源與 remesh 收據、陳列支撐面及實際模型近景；食品需有餐具或盛盤，吊衣需掛在吊桿，不能懸空。使用者本輪要求商品模型不超過 100 種，複用已完成元件，缺項使用 Tripo Model 補足；必須分開記錄模型種類及重複擺設數量。

- 最終紀錄：10 層／80 櫃完成裝潢改版；全部 80 張實際模型近景及 10 張逐層接觸表已檢視。幾何驗收 76,797 項、Unreal 實體碰撞與動線驗收 43,486 項、交付檢核 890 項通過；單層最高 91,663 三角面。160 個實際 Unreal 區域燈與所有專櫃三語文字已重開驗證，同時確認既有 387 個樓層語言材質及 34 面校園館名招牌。最新證據：`output/street-social-city-production/mall-v1/boutiques-v1/acceptance.json`。

- 完成版近景揭露：若所有專櫃展示櫃沿用同一木材，牆面即使不同也仍會像制式展售間。櫃體需依品牌分成石材、深色漆面、精密金屬及細木作，餐椅坐墊與椅背另外使用軟包材質。懸掛招牌只在正面配置文字，背板與邊框使用無字材質，避免室內看到鏡像反字。

- 首輪人眼高度渲染發現：只加背牆會留下大片無裝修天花與缺乏邊界的空間，仍像臨時陳列區。需補齊實際天花、側向襯牆與開放門楣；不能用概念圖冒充實際完成。首輪入口射線亦抓到 M1-0607 咖啡機在正中入口，已改到後場並按南北朝向旋轉，增加逐櫃 2m 入口檢查。

- 80 個櫃位逐一記錄背牆構造、木作／石材／金屬／織物搭配與商品展示方式。公共區一致、櫃位各有品牌語言；不套用商店街的做舊、任意塗鴉或凌亂裝飾。
- 盤點發現南排餐廳原先沿用北排廚房座標，會把廚房放到開放店面的前方。必須按店面朝向鏡射後場位置、門向、設備朝向及餐桌避讓，不可只鏡射招牌。這次一併修正九、十樓的南排餐廳。
- 專櫃招牌原 8×0.8m 與 2048×512 貼圖比例不符。改為 4×1m，UV 依實際寬高設定，三語文字隨精品配色產生，避免再被一般語言貼圖產生器覆蓋成統一底色。
- 原生百貨重新匯入採內容雜湊子資料夾。AuditOnly 必須從場景中實際 Actor 取得 StaticMesh，再核對來源 SHA；不能讀舊的固定根目錄資產而誤驗舊模型。



### 2026-10-18 商品交付與總覽收據同步
百貨加入商品後，舊 localization-v1/acceptance.json 仍可能列出前版 91,663 面及舊地圖 SHA。不可將舊總覽當作本輪驗收；verify_street_city_boutique_delivery.py 現在只在當前幾何、原生地圖、三語、逐櫃圖片及商品驗收全部通過後，才更新總覽與商品 production-status.json。失敗時不寫 complete。


### 2026-10-18 Unreal 匯入紀錄需區分來源驗收與引擎診斷
本輪 Interchange 在各樓層匯入時記錄 `Invalid input AlphaMode for material expression node MF_Default_Body`，但匯入完成，原生碰撞驗收通過。引擎原始碼將 AlphaMode 作為 Substrate 函式輸入；這類函式資產／引擎版本診斷不得以刪除貼圖或關閉安全設定處理，也不得把碰撞通過宣稱成引擎完全無診斷。商品 PBR 以原始 GLB、組裝保留與實際 Blender 圖驗收；遊戲內特定透明材質若有視覺異常需另查函式資產相容性。


### 2026-10-18 引擎輔助檔不能是未驗證的單點依賴
原生重開驗收發現 Tools/ok_engine.ps1 不存在，啟動器在進入 Unreal 前就失敗。run_street_city_mall_unreal.ps1 改為先使用存在的輔助檔，否則依 OK_ENGINE_ROOT、engine.local.json、預設引擎目錄解析，並在啟動前驗證 UnrealEditor.exe。保留使用者設定，不重建或覆寫遺失的共用輔助檔；失敗的啟動不能算成本輪原生通過。


### 2026-10-18 全城整合必須逐 Actor 核對當前來源
原三語驗收確認了材質綁定，但不能證明所有場景模型都是磁碟上的最新版本。本輪獨立比對 419 個模型 Actor，發現商店街 223 層仍指向舊 SHA，其他分類相符。新流程以當前採用來源 inventory.json 作獨立基準，先準備變更資產，再保留位置與比例更新 Actor，重套三語並重開驗收；不能以舊匯入收據或單純 Actor 數量宣稱最新整合。

原生驗收程式從 Entry 小地圖啟動，避免啟動時自動載入全城後再重載一次。在其他工作佔用大量系統記憶體時，不同全城編輯器驗收應控制同時執行數量；只結束本任務已核對 PID 與命令列的程序，不動其他使用者工作。Runtime 偏好測試採隔離 UserDir，避免控制台測試修改使用者真正的語言選擇。


### 2026-10-18 Runtime 驗收不可假定 Blueprint 內部 API 會匯出給 Python
USubsystemBlueprintLibrary 的 BlueprintInternalUseOnly 函式未以同名 Python 類別匯出；測試改走正式公開 CityLanguageLibrary 與真實 GameInstance，不新增只供測試使用的 runtime 入口。Unreal ExecutePythonScript 在頂層結束後會還原 __file__，非同步 callback 必須事先捕捉 SCRIPT 路徑；並設定 set_keep_python_script_alive(True)，否則下一幀會關閉。任何測試 callback 錯誤都不能拿編譯成功或舊收據當成功。


### 2026-10-18 PIE 材質測試必須先確認 Collection 不是空值
先前測試在 PIE callback 內使用 EditorAssetLibrary.load_asset，該編輯器介面在遊戲期間回傳 None；MaterialLibrary 對空 Collection 回傳 0，導致繁體預設值假通過、簡體看似失敗。正確作法是在 PIE 前使用通用 unreal.load_asset 取得並保留實際 MaterialParameterCollection，先斷言類型非空，再檢查三個實際值。這是測試工具的錯誤，不能憑這個結果判定 runtime 模組沒有套用。模組同時改為明確接收目標世界並記錄完整世界／Collection 路徑，方便區分多世界問題。


## 2026-10-19 Remaining-world production
- Reusing a type-specific adapter without replacing its research registry can leave invalid references or raise a KeyError. New adapter has an independent registry and 8-building contract; existing campus sources remain untouched.
- fit_lines returns font, lines, step (three values); unpack all three. First sign preparation failed before writing outputs; corrected and regenerated 232 signs.
- On Windows rg cannot expand a wildcard embedded in a directory/file argument; use rg Tools -g matching-pattern.
- Planning status is not mesh acceptance. The new delivery remains unapproved until actual GLB reopen, routes, stair/headroom, signs, and native import pass.

- H1 initial F03 reached 120,896 triangles. Production export stopped. Repeated furniture must receive independently measured reusable LODs, not a larger floor limit.
- A remesh script may exit successfully but emit rejected_requires_repair; only street_city_remesh_gate.verify can adopt it. The first bed 800-triangle attempt failed geometry and budget; never adopted.
- Soft bed LOD below 1,050 triangles lost more than 50mm of its cushions; the selected 1,050-triangle source has measured 41.33mm max shape change. This is a declared soft-furniture LOD tradeoff, not structural tolerance. Sanitary LOD tolerance is 15mm toilet / 20mm basin; architecture remains exact metric. The 300-triangle basin measured 15.93mm and was rejected by the initial 15mm trial; the explicit 20mm furniture tolerance is retained in its measurement, followed by the unchanged 5mm Rust remesh check against the LOD source. Visual review remains required.

- The inherited campus lift car was 3.3m wide while H1 shafts have only 2.4m clear width. Remaining-building cabins now use each shaft's car_clear_size_m; distinct shaft IDs prevent duplicate A/B cabin identities. Static cabins only, no gameplay wiring.
- New H1 north entrance initially left an inner wall 0.3m from its cut plane; move its mounting plane to the actual exterior wall centre (0.1m inset). Door ray tests caught this. A reception counter also clipped a service door approach; relocated before passing.
- A label saying water is insufficient: water surfaces are now separate non-colliding exports, not hidden within the solid floor model. CV aquarium tanks use transparent wall assemblies with separate water and genuine Tripo fish instead of opaque cylindrical proxies.
- PK obsolete positions predate the 50% world compaction. Use world-terrain-layout world_center_m, then sample current actual height field. Old basketball placement overlapped the expanded 400m pitch; moved to a free teaching-zone plot. PK-0103 is allowed only inside E1's explicit certified open courtyard.
- Blender bundled Python lacks Pillow; do not import terrain authoring modules solely for a bilinear sampler. The runtime sampler is independent and uses the same numeric formula.
- Standability probes must begin above the actual finished deck surface: wood deck planks are 0.06m above the platform datum. The first probe at +0.04m incorrectly treated the deck as an overhead obstruction; corrected to +0.10m, while retaining actual downward support and 2.16m upward clearance rays.
- Browser input stalled on a stale Chrome tab. A fresh Tripo tab using the documented browser API restored control; no security settings changed. All four generated components downloaded to the actual Downloads folder and were checked as complete GLBs before copying.

- Unreal Python has no numpy. Native asset-preparation scripts must read baked JSON contracts and stdlib paths, not import NumPy-dependent authoring modules merely for an output constant. First outdoor native attempt stopped before producing a receipt; corrected and freshly imported all34.
- Missing-building ground foundations were never included in the original campus/shop terrain pass. Rebuilt five affected ground chunks, adding all8 footprints and actual entry slabs. H1 concept datum8m was4m below adjacent streets; corrected to12m in current model context and terrain pad, keeping every interior dimension unchanged. Avoid creating inaccessible subgrade doors or underground fake ramps.
- Exact foundation verification found a25mm finish offset at CV west entry. Confirmed the hit object is an existing asphalt route and its designed elevation is +0.025m, then verified that explicit threshold; all other entry slabs retain3mm checks and all terrain seams retain2mm checks.

- The legacy immediate sync commandlet stalled after map loading with no actor-level progress and low CPU. Verified PID100460 exact sync command and unchanged map SHA against its backup before stopping that owned commandlet. Replace immediate bulk spawning with a deferred, logged, checkpointed editor tick workflow; never terminate unrelated editors. No scene was lost.

- The map-load stall was bypassed by the engine-supported -NoAsyncLoadingThread and -NoAsyncPostLoad flags (verified in installed AsyncPackageLoader.cpp); editor-only deterministic import/audit settings, no project or security preference changes. Deferred placement then exposed Slate callback reentrancy: actor spawn/save can pump nested ticks, causing duplicate placement and failed nested saves. Add a busy guard around every callback body and source-hash-proven cleanup of only this delivery's duplicate actors. Engine exit0 is not a successful task receipt; old sync.json remained stale and was never accepted.


2026-10-19 戶外最後視覺驗收：神社舊址跨越陡坡，雖有支撐但出現不合理的高腳平台。改採附近 [85,1033] 的實際平坦森林高地，24×18m 九點地面均137m；手水盆改成有底、四壁與開口的組件。已採用模型不得原地覆寫：以建模程式雜湊建立新版本來源，再驗收與換綁。地面路網原先會穿入 CV 等建物，已以全部建物 footprint 從鋪面生成中扣除並重新 remesh。

2026-10-19 驗收工具修正：Unreal 延後 callback 執行時 __file__ 已不在腳本全域環境，改於載入時保存 verifier SHA。模型檢查不能因編輯器退出0就算通過，必須讀取本次新寫入的驗收收據。渲染中的 PNG 尚未寫完不可製作縮圖；需等 render-receipt 與程序結束才讀取。

2026-10-19 全城三語重開驗收：73個新房間材質共用了相同圖像快取，導致 CityTextID 指向另一房間。通用修正為 material identity 必須包含 semantic_id，不能僅比對來源材質和三語圖片SHA。維持嚴格的每房間ID驗收，不放寬成只看圖一樣；重新安裝remaining-all後再次獨立重開。

2026-10-19 執行期語言回歸：舊 snapshot 將 Unreal Transform 暫存 struct 的 repr 當成幾何證據，repr可能含記憶體位址。改採明確的座標、旋轉、縮放浮點值與mesh/material路徑，比較真正資料；如仍不同輸出逐Actor前後差異，不降低實際幾何保持不變的要求。

2026-10-19 Grok牆材質：CLI 1.0.24會去除提示檔結尾換行，Windows提示檔亦含CRLF，舊逐位元提示比對因此漏收真正成功的image_gen收據。比對仍先要求提示檔SHA正確，再只容許CRLF正規化及移除結尾換行，並核對唯一image_gen tool call與其原始檔SHA；禁止重新花費生成或僅憑檔名聲稱成功。

2026-10-19 牆材質展示渲染：正式 floor audit 的層高欄位是 floor_to_floor_height_m，而非組裝adapter的height_m。展示程式已改讀正式欄位，失敗的展示版本保留但不採用；實際模型幾何未修改。

2026-10-19 牆材質原生套用在編輯器啟動時立即load_level又停在初始化。已核對PID94996為本次owned程序、地圖SHA仍為8fc60f0e...且備份完整，僅停止該程序。改為60tick後才載入地圖，callback先解除避免save重入，並加40秒Python堆疊紀錄；其餘程式與使用者瀏覽器不動。Native法線BC5材質按RG重建Z並明確設Normal sampler，避免因RGB假設變成反向法線。

2026-10-19 真實RHI驗收發現兩項牆面問題：1. glTF原材質以MF_Default_Body多輸出函式連接，僅get_material_property_input_node再連空白output會把BaseColor接回Normal，形成PixelNormalWS循環。改用GetMaterialPropertyInputNodeOutputName取得實際BaseColor／Roughness／Normal腳位，不猜第一輸出。2. UE會將偏藍的IN建物底色誤辨為NormalMap，導致sRGB被強制關閉。每張圖必須依角色明確指定TC_DEFAULT或TC_NORMALMAP，再設定sRGB；不能只信匯入器自動判斷。先前RHI輸出是黑圖且編譯失敗，未採用；改分frame建立、capture、export，並以BaseColor GBuffer核對材質實際色彩，正常PBR照明仍由Blender實際模型預覽呈現。
# 2026-10-19 教室、百貨外牆與行政設備修訂

- 第一版新功能原生驗收在load_level返回的同一tick立即射線，導致全部319外牆及全部教室踏步／地板支撐都miss，而來源與材質檢查均正確。開圖、完成資產載入與等待60tick後才執行物理查核，和既有原生碰撞驗收保持一致；保留原射線位置與命中標準，不把miss改成通過。

- 後續發現直接ExecutePythonScript的NullRHI PIE也有同樣關閉例外，不能歸因於D3D12材質。將GPU擷取與PIE測試都統一改由延後、隔離runpy模組入口執行後，D3D12擷取與NullRHI的30項真正PIE測試皆重新取得退出0。預設GPU擷取恢復D3D12，D3D11保留為明確可選後端；程序退出碼與實測圖檔分開記錄。這證明目前批次路徑已恢復，未取得原生堆疊前仍不把推測的Python生命週期當成確定根因。

- D3D12批次擷取兩次均完成11張圖、無材質編譯錯誤，但在引擎關閉後回傳0xC0000005存取違例；未把這些結果當成完整通過。保存失敗日誌與退出碼，批次擷取改採可選D3D11後端，不改專案遊戲RHI設定；父程序只有在引擎退出0、圖片新寫入且SM5／SM6無編譯錯誤時才將收據升格passed，出圖本身只列captured。底層D3D12關閉例外未取得原生呼叫堆疊，不能宣稱其根因已修復。

- 追加社團、銀行及站務空間後，原117層批次成為舊方案。先確認地圖仍為原bd1f版本、僅停止本任務尚未寫圖的sync程序，保留已匯入資產，改以最終119層來源統一採用與重驗；其他正在執行的遊戲程序保持運行。中止收據不能列為完成證據。

- 單靠functional_programme.kind=admin會漏掉社團辦公室、銀行服務、交通站售票／站務等有行政工作台的空間。追加語意盤點並做必須覆蓋的集合驗收；不能把「行政客房」誤當辦公室，也不能把洗衣機的console當工作桌。每個修訂資料夾保存實際生成器原碼與SHA，規則更新不得覆寫舊來源證據。

- 大圖在ExecutePythonScript初始第2個tick直接load_level可能停在FlushAsyncLoading，即使已關閉非同步載入執行緒也會出現。統一原生批次入口先等待90個Slate tick再執行真正腳本，保留每項腳本獨立新收據門檻；不可把卡住的程序或舊收據說成成功。
- 沒有功能變更的 IN、SP、R1、R2、TR 保留原已驗收來源。SP透明運動網的glTF重匯會改變索引分裂順序，嚴格位元幾何檢查已拒絕採用那份重存版；不為無關需求重存或放寬該檢查。

- 原百貨外牆大量透明窗格會讓室外光進入，與本輪需求不符。改成有實際180mm厚度、Alpha=1且Transmission=0的實心面板，保留入口，不以只改顏色或關閉全場太陽遮掩。
- 原普通教室有桌椅和講桌，卻缺黑板／投影幕；較大教室也沒有階梯平台。依房間用途批次補設備，階梯模型必須同時驗踏步、走道、無障礙前排與淨高，不能只抬升座椅。
- 原行政工作站不少只有螢幕而沒有鍵盤等輸入設備；百貨管理室只有空桌。必須以實際桌面支撐驗收完整電腦，不只以programme標籤算具備設備。
- H1接待大廳初次盤點沒有任何可用工作桌，第一版驗收因此拒絕。利用已配置的沙發包絡替換一張真正接待桌與筆電，禁止略過該空間或將電腦放在半空。
- Blender任務包裝器將SystemExit視為失敗；續作快取判斷改到外層執行器，避免已成功的工作因腳本提前exit被誤報。失敗收據保留，不人工改為通過。
- GLB候選素色面必須以幾何面向和part_kind複核；家具、天花板厚邊與球場標線不可當成牆面漏做。


## 教室前牆修正：大黑板與中央垂降投影幕
- 錯誤：把黑板與投影幕左右並排，未符合使用者指定的大學教室配置。
- 通用修正：完整連續大黑板置中（寬 6–8m、高 1.5m），中央垂降投影幕寬 3.6m、高 1.8m；布幕朝學生方向前移 0.16m，放下遮住中央黑板，兩側保留書寫區。捲軸在布幕正上方，投影機沿中央線。
- 驗收：重新匯入實際 GLB，依材質分離頂點檢查置中、大黑板尺寸、遮擋範圍、前後距離與捲軸位置；更新全部教室模型、Unreal 原生場景、來源雜湊與實際渲染。不能只修改示意圖。

- 修正驗收結果（2026-10-20）：183 間／11 棟教室全部更新；模型 137,012 項、Unreal 原生 10,092 項、全城整合 1,023 項、PIE 三語切換 30 項皆通過。實際模型渲染已目視確認中央投影布遮住完整大黑板。最新入口：`output/street-social-city-production/functional-interiors-v1/index.html`。

## 街景製作：瀏覽器下載事件與配置密度（2026-10-20）
- Chrome下載事件可能逾時，但不代表下載失敗。事件 Promise 必須捕捉錯誤，等10–30秒後先檢查實際 Downloads 的檔名、大小、GLB檔頭；禁止因此重複生成扣點。
- 首輪街景候選過度優先樹木，座椅與車輛被占位排除；改成樹列、燈具與停留設施交錯配置，店門前沿立面側邊設施帶，保留真實入口連接路徑。

- 配置效能修正：NPZ 的陣列採惰性解壓，逐點讀取會反覆解壓同一地形。設計開始時只載入一次所需陣列並保留記憶體副本，再建立道路空間索引；不可用降低驗收密度掩蓋效能問題。

- LOD不可只依比例減面：先保護外輪廓與測量誤差超標的細部，再確認面數；近景保留自行車原版本，遠景才採3,000／1,000面。最遠樹LOD以遠距畫素尺度明列容許差，不套用在建築交通幾何。
- 樟樹低面四邊網格觸發 Rust Broken irregular face chain，保留失敗日誌後改為工具支援的真實三角求解。原始機車細部在5mm保護下無法壓到6,000面；先建立明確20mm街景LOD來源，再對該來源以5mm誤差門檻Rust remesh，原始下載與失敗候選保留。
- 鋪面與家具不能共用固定抬高值；先查地形角點，坡地拒放平板，構件基準貼合真實地形。

- LOD高度驗收抓到櫻花樹遠景縮短：減面後殘留未被面引用的極值頂點，Blender量測含它們但GLB匯出會剔除，造成假尺寸。修正為先刪除無面頂點，依實際可渲染表面維持垂直包絡，再測誤差與Rust remesh；獨立重匯入驗收不得以來源報告數字替代。

- Unreal 內建Python沒有NumPy；公用契約／JSON工具不得在模組頂層匯入科學運算套件，僅配置函數需要時才載入。
- Unreal Transform 建構函數的rotation輸入是Rotator，不能直接傳Quat；寫入前使用已確認的原生簽名，保存後再逐實例驗收位置、角度與尺寸。

- 街景配置獨立外包圍驗收抓到5株旋轉後樹冠伸入建築：排除距離改用樹冠對角線半徑，而非未旋轉寬度的一半；全圖重排後43,553項幾何配置檢查通過。
- HISM批次加入曾得到0實例：對Blueprint元件使用set_editor_property觸發PostEditChange，會重建Owner並使舊元件參照失效。改用公開runtime setter指定網格、材質、陰影與剔除設定，必要時重新取得活元件；不能只看到Blueprint Actor存在就當成擺放成功。

- 街景實際渲染顯示早期樹冠像晶塊，原花店花卉仍帶金屬桶，不適合直接當花台植栽。最終樹改用原創透明葉卡＋認證枝幹，花卉裁除容器並重新Rust remesh；舊草稿圖片不得當最終視覺驗收。透明葉卡須檢查GLB MASK／雙面及原生材質繼承、alpha coverage mipmaps，避免遠景葉冠消失。
- 原生街景替換必須同時更新場景來源契約。16個舊區域樹Actor採隱藏、無碰撞封存，8個複合設施只移出原樹並保留其餘世界三角面，再以原位HISM恢復樹木。牆面安裝的inventory SHA也需重綁，避免新模型與三語／主題牆驗收互相失配。

- 全城首次D3D12擷取需建立大量貼圖快取；舊擷取在建立SceneCapture後長時間沒有進度，已只停止本任務street-render-user程序並保留失敗退出收據。擷取腳本補上逐階段日誌、先停用自動及移動擷取、明定650m視距，再重新實際擷取；不得把中止、黑圖或舊圖片當成功。

- 首次真實Unreal街景抓到葉卡矩形底板：僅驗GLB MASK與原生BlendMode不足以證明Alpha有接上。新增street_city_native_foliage.py，直接匯入原創RGBA、保留alpha，RGB連BaseColor、A連OpacityMask、雙面及0.33閾值。獨立原生驗收進一步讀實際材質輸入節點與輸出通道，不以metadata或材質名稱替代。暗部同時重新指定有效天空Cubemap及補光強度；修正後必須重拍實際城市。

- 原生材質槽Array的迭代struct為複本；只改slot.material_interface再寫回整個舊Array，會保留原材質。葉片改用StaticMesh.set_material(index, material)，立即讀回確認；保存後重新驗證每個HISM覆寫與網格材質相同，再查TextureSample的A輸出接OpacityMask。原生匯出TGA與來源RGBA均保留約56%透明像素，證實問題在槽寫回而非來源圖片。

- 同步探測腳本未明確quit_editor，留下兩個本任務探測程序，導致後續保存同一材質發生Windows分享鎖Error32。已只關閉本專案且命令列明確為leaf-alpha probe的兩個程序，加入正常退出並串行執行。收據產生不等於程序已退出；必須確認整個程序成功結束才啟動下一個原生寫入。未停止使用者其他程序。

- 成果頁的認證元件可能位於E槽，頁面在C槽；Windows relpath無法跨磁碟。連結產生器對同槽使用相對路徑、跨槽使用完整file URI，發布後逐一驗證本機href/src存在，不因網頁生成失敗而略過模型連結。
