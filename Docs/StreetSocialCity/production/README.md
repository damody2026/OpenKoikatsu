# 大學城 V2：逐房拆件、Grok透視三視圖、Tripo模型與組裝

日期：2026-10-12。最新本輪完整結果：[current-acceptance.md](current-acceptance.md)。全城配置資料已完成尺寸與用途驗收；生成工單不等於已生成模型。當前報告：[尺寸與通路](layout-acceptance.md)、[用途／設備／戶內隔間](functional-acceptance.md)。

開啟 [review.html](review.html) 查看完整世界地形、74棟／283層SVG、尺寸、設備、互動及逐棟來源；[index.html](index.html) 保留製作流程與早期樣張。當前外框以 world-terrain-layout.json 為準。尺度與牆厚核對見 [scale-audit.md](scale-audit.md)：先按用途求合理淨尺寸，再推算建築外框；房間尺寸為淨空，核心外框與扣牆淨空分列，牆厚設計0.2m。百貨當前150×100m，每層8店；四人寢室主要單元8×9m，住宅10×12m，豪華客房12×10m。這些是設計值，沒有冒充來源場館實測值。

## 製作順序

0. 每棟先搜尋中國或台灣同用途場地的內部配置。逐棟對應 [BUILDING_REFERENCES.md](../BUILDING_REFERENCES.md)，記錄已讀圖面、尺寸證據、配置推導及缺項。索引、照片與比例平面分別標示；沒有資料不能捏造實測值。功能核讀、控制幾何、實際模型分開驗收。
1. 以房間ID建立精確控制幾何：淨尺寸、門洞、窗洞、床位、家具數量與位置。套房先分主臥、客廳、衛浴、私人湯池等功能區。
2. 同一控制幾何固定三個透視相機：前側西南視角、後側東北視角、高角度東南俯視；只改相機和前景牆顯示，不另設計三個房間。
3. 用Grok image_edit參照控制圖補材質與外觀。門窗、家具數與位置發生變化時退件；圖片不承擔尺寸精度。
4. 以房間全景作為裝配參考，再把空房外殼、門扇和每種可互動家具分開生成。空房外殼與樓板是語意總成，禁止直接送Tripo；manufacturing-manifest.json記錄先扣洞再拆分的最多6m牆段、地坪及屋頂片，tripo-modular-jobs.jsonl保留真正結構部件工單。可重複的同一張桌椅只生成一個經驗收模型，按實例座標複製；不同房間仍保留獨立room_id。
5. 每個零件建立同樣的三視圖及獨立乾淨輸入圖，再送Tripo。不可將整張三格圖直接送入單物件模型生成，避免生成三個房間或三份重複物件。
6. 檢查模型、清理拓撲、修正尺度與pivot、保留PBR貼圖。外殼須分離地板、牆面與可拆天花；開門與互動家具保持獨立物件。
7. 依assembly-manifest.json組裝零件成房間，再組樓層、公共核心、外觀模組與建築；最後放回世界座標並接街道和景觀。
8. 引擎驗證每個入口、門扇、碰撞、導航、雙人站位、互動與睡眠條件、串流、LOD與存讀檔。

## 透視三視圖和Tripo輸入的區別

Grok的前側、後側、俯視透視圖用於人類核對及零件設計。Tripo多視圖API的view-key為front、left、back、right，並沒有high-top欄位；不能把俯視圖謊稱側面，也不能把任意對角視角當成已校正的正面。正式提交前，需從經核對的零件幾何或參考圖取得方向正確的獨立圖；只有一張可靠圖時使用單圖入口。

已核對的官方來源：

- [Tripo H系列多視圖API](https://developers.tripo3d.ai/en/docs/generation-multiview-to-model/standard)
- [Tripo P系列多視圖API](https://developers.tripo3d.ai/en/docs/generation-multiview-to-model/p)

模型版本與價格必須在實際送件時重新核對。本次沒有送出Tripo任務，也沒有把任何本機路徑冒充上傳token。

## 全城拆分清單

當前數量由重建程式產生於 [validation.json](validation.json)，以其中的 `part_generation_tickets`、`assembly_instances` 與 `roles` 為準，不在文件保留會過期的手抄統計。

1,156筆為主要互動單元（1,136室內＋20室外），不是1,156個實體房間。每個主要單元可能另有獨立門位、牆體與設備的附屬分隔空間；完整清單見 [building-floor-audit.json](building-floor-audit.json)。反覆放置的同型家具仍可共用核定資產。占位設備、缺乏功能審查或未核定尺寸的零件不能直接送生成。

JSONL只記錄工作，不會自動提交數千筆生成或重複付費。每個零件的reference_status、tripo_status、task_id與驗收檔獨立追蹤。實際紀錄保存在 generation-ledger.json，重建圖面不會清除；只有幾何簽章仍相符的紀錄會套回本輪工單。參考圖、Tripo生成完成、下載及模型驗收分開記錄。

## 歷史樣張：EE-0101（非本輪配置）

位置：output/street-social-city-production/EE-0101/（相對專案根目錄）。本輪家具配置已更新，下列圖片與簡模僅作歷史流程參考；當前所有Grok工單均需按最新控制幾何重新生成。

- 第一版 EE-0101-perspective-three-views.jpg：純文字Grok生成，三個方向與桌椅數量不一致，退件，不可送Tripo。
- control-three-views.png：同一個12×10×3.6m控制幾何的三個透視相機；12張學生桌、12張學生椅、一個講台、書櫃和雙人討論桌。三個方向用同一份幾何。
- EE-0101-grok-controlled-three-views.jpg：Grok image_edit依控制圖產生第二版。已人工檢查為三個不同方向、保留3×4桌椅排列；仍需逐零件獨立圖與進一步模型驗收。螢幕色彩等外觀有變動，幾何與尺度以控制檔為準。
- EE-0101-layout-proxy.blend／.glb：控制簡模，明確標示非Tripo結果。
- grok-run.json與grok-controlled-run.json：真實CLI執行紀錄。第二次執行已產生圖檔，但在額外檢查時到達8回合限制；沒有因此將待驗收狀態冒稱為全流程成功。

## 組裝座標契約

- 產線採公尺；Blender中X向東、Y向北、Z向上。glTF匯入／匯出由Blender處理Y-up轉換；UE世界座標為(北,東,上)×100公分。
- 建築原點為底層中心；房間原點為室內地板中心；家具採底部中心pivot，門扇採底部鉸鏈pivot。
- 每層沿既定完整外框配置，主走廊淨寬4m、側走廊及支走廊至少3m。E1依中庭翼樓旋轉分隔。運動館泳池跨1、2樓，2樓看台開孔且上下對位；泳池與其他運動區有實牆，池畔側門通同層更衣、淋浴及廁所。不得單獨縮放室內外框或把挑空填成樓板。
- 每層兩側各預留6m公共核心及3m側通道，樓梯、電梯井及梯廳上下對齊。各建築外框與世界圖一致；任何改動必須同步更新世界地形與樓層資料。
- 當前每個主要活動單元與附屬房間都有用途設備及座標；住宿單元有實際臥室、衛浴、3m戶內走廊，住宅另有廚房。資料層已驗證設備不相撞、門位可達、必要設施與面積比例；實際網格碰撞、門扇掃掠、機電與遊戲導航仍須在模型階段驗證。
- 外牆窗洞必須根據實際建築位置生成。EE-0101示範控制圖的東牆窗是房間樣式研究，不代表每個房間都能向東開窗；相鄰房間的共用側牆不可保留外窗。
- floor-layouts/*.svg畫出淨尺寸、門洞位置、公共走廊與核心預留。尚非完整建築施工圖；樓梯尺寸、入口與落地平台、外觀窗表、衛浴及機電仍需深化。

## 組裝器

工具：Tools/assemble_street_city_parts.py。資產目錄：Content/IndependentResources/StreetSocialCity/assets/<asset_id>/。

每個通過驗收的模型需有approved.glb與approved.json，後者至少包含：

```json
{"approved":true,"pivot":"floor_center","axes":"X east,Y north,Z up","model_sha256":"actual GLB SHA-256","measured_size_m":[0.8,0.6,0.75],"geometry_signature":"current asset signature","tripo_model_id":"actual Tripo ID","quality_review_file":"path/to/quality-review.json"}
```

門扇pivot需為hinge_bottom。approved=true必須建立在實際模型驗收，不能為了繞過缺件檢查隨意填寫。

正式組裝指令（以電資館為例）：

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe' --background --factory-startup --python Tools/assemble_street_city_parts.py -- --building EE
```

缺件、未驗收或座標契約不符時，組裝器停止並寫出missing-assets清單。正式模式不會自動生成方塊來冒充Tripo模型。

明確加--proxy才輸出控制模型：結構使用實際牆段與樓板孔洞；未生成家具以顯示用包絡標記，不把走廊或電梯井做成實心箱子。Tools/build_street_city_controls.py可依清單批次建立全部建築BLEND／GLB並驗證各棟房間數、來源SHA及產物；紀錄於output/street-social-city-production/control-assemblies-acceptance.json。這些檔不是Tripo模型，也未通過實際遊戲導航。

## 目前阻塞

Grok與已登入的Tripo網頁入口已接通。書桌 b5e24e87-057a-43ff-9259-a200fa9c9e70 的真實STL已下載，原檔及SHA保留於output/street-social-city-production/downloads/COMMON-CITY-study_desk/。方向校正、0.8×0.6×0.75m尺度、局部網格修補、UV及本地製作PBR均已通過靜態零件驗收，登錄至正式資產庫；本地PBR不冒稱Tripo原始貼圖。LI-0508房間四次實際擺放已核對頂點尺寸與座標，灰色結構仍是控制模型，房間未完成。

座椅 beae71da-dbbf-4b21-9706-8117f9a31f58 已由Grok獨立參考圖送H3.1生成（30點、42,998面）；STL／GLB匯出有成功通知，仍未收到實際檔案。帳戶頁顯示Pro，原因未確認，不能算可組裝模型。當前教室Grok三視圖因多加講台及背景牆退件。完整證據見generation-progress.html、generation-ledger.json及production-queue.md。


## 共用模型與真實候選驗收

所有同型同尺寸家具共用穩定資產ID，每間房的位置及旋轉仍各自保留。跨城市、運動及衛浴型錄的相同物件亦合併。`asset-library-acceptance.json`逐項核對共同資產、參照與實際生成紀錄；重建流程會自動執行。

`Tools/prepare_street_city_candidate.py`在Blender匯入GLB／STL／OBJ，保留原檔SHA，記錄方向與各軸縮放，輸出未批准的候選檔。各軸比例修正差超過5%則拒絕候選，必須先查方向或重新生成，禁止用大幅拉伸掩蓋比例錯誤。`Tools/render_street_city_candidate.py`渲染實際下載網格三視圖。兩者不能自動產生approved=true。

只讀檢查命令：

```powershell
python Tools/inspect_street_city_model.py --asset COMMON-CITY-study_desk --file output/street-social-city-production/downloads/COMMON-CITY-study_desk/oriented/candidate.glb --report output/street-social-city-production/downloads/COMMON-CITY-study_desk/geometry-acceptance.json
```

正式組裝器會再次直接量測GLB，不信任JSON尺寸。它要求生成紀錄actual_model_approved、參考圖SHA及TripoID一致。quality_review_file須含同一模型SHA以及silhouette、topology_normals、materials_uv、submeshes、interaction_clearances五項實際驗收結果；任一缺項或不符就停止並輸出rejected-models清單。尺寸檢查不能代替模型品質或遊戲走查。

`Tools/audit_street_city_candidate.py`以真實三角面檢查拓撲、法線、有效UV、實際綁定PBR影像及使用淨空。`Tools/finish_street_city_candidate.py`依資產配方限制局部修補位移，製作UV及本地PBR，輸出仍未批准的候選。三角度視覺檢視完成後，`Tools/register_street_city_model.py`核對整條來源證據再登錄靜態資產。任何一件通過都不代表碰撞、LOD或全城已完成。

`Tools/build_street_city_production_queue.py`展開製造配方，依實際批准檔案整理逐房缺件及可組裝狀態。`Tools/render_street_city_room_progress.py --room LI-0508`僅作已驗收零件的局部擺放預覽；灰色控制結構及缺件必須保留標記。`python Tools/verify_street_city_current.py`核對現行來源、全部控制GLB哈希、正式資產門檻、目錄尺寸及回歸後，產生current-acceptance.md／current-feature-acceptance.json；舊來源報告保存在acceptance-history。

所有候選產線透過`street_city_catalog.py`共同讀取語意資產及製造拆件，核對來源SHA並拒絕重複ID。`render_street_city_controls.py --asset MOD-05687eb6636cb9a1`可繪真實尺寸的結構拆件控制三視圖；控制圖仍非Tripo模型。未定義真實形狀的空間或機構不允許輸出空白圖冒充參考圖。


## 緊湊世界V4

世界長寬各為原50%，現行1.1×1.1km；SVG地圖區每公尺2單位，地圖區原點(100,215)，SVG metadata與world-terrain-layout.json均有換算契約。建築與完整樓層仍使用相同公尺外框，不把人、家具、通路或池長一起縮小。

學生街48棟三層店屋，百貨150×100m、十層各八個主要租戶，附近O01–O03三棟六層辦公樓。新增建築有逐層SVG、用途、互動、廁所、樓梯、電梯與後勤配置；中庭、步道、野餐、舞台及營位已避開建築與幹道。完整驗收見site-acceptance.md。

商店街沿三條共用步行街設58個相向主入口；第二排前後雙入口，下方兩排相向。門淨寬1.8m、高2.7m，門前通路及室內側通道至少3m。共同來源為Tools/street_city_site.py，地形、1F圖、門資產、立面開口工單與組裝座標同步；詳見[相向入口表](student-street-frontages.md)。獨立街區稽核核對實際SVG、相向門位及路徑不穿建築；控制GLB驗收另檢查58個入口內側2m的實際幾何淨空。這些檢查不表示Tripo門扇、立面或遊戲通行已完成。

Tools/build_street_city_controls.py先核對全部室內輸入及既有GLB哈希。只有戶外節點等不影響室內控制的資料改動時，可保留控制網格並重新檢查全部頂點；位置、尺寸、資產或拆件配方變動則重新匯出。任何重用都不能取代正式Tripo模型或遊戲導航驗收。
