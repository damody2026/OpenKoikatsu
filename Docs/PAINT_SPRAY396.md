# Stage396 白色油漆／水槍：低面數材質遮罩

目前使用原本 **3,454面** 示範人物，已撤回10萬面版本。污漬形狀由獨立 **1024×1024雙Render Target遮罩**決定，覆蓋材質貼在原身體表面；身體與覆蓋層不為畫污漬細分。引擎實際版本UE5.8.3，D:/UE5.8。

## 操作與資產

- 游標在3D畫面，按住A，再按住左鍵持續噴射；放開任一鍵停止。UI起始拖曳不能轉成噴射，文字搜尋時不攔截A／R。
- R清除人物遮罩、附著／飛行滴、Niagara與地面污漬。
- 頂部「油漆設定」可調速度、流量、散射、黏度、表面速度、滴落門檻、附著秒數、滯留厚度、更新頻率與顏色。
- 地圖 `/Game/PaintDemo/PaintDemo`；模型／骨架／Niagara／材質位於Content/PaintDemo。人物是自製六骨模型及C++待機／動作，未依賴外部人物授權。
- Tools/build_paint_demo.ps1可重建。builder載入既有資產時不覆寫；修改資產生成邏輯後要先備份對應資產再重建。DefaultGame.ini列入AlwaysCook；正式封裝尚未實測。
- 已開啟的Editor仍可能載舊DLL，重新開啟後依目前manifest載入新版本。

預設噴流宽3.5mm、短條12cm，主液柱Flow×200／分離水滴Flow×20；流量12cm³/s、速度1600cm/s、散射2°。黏度8、表面速度8、殘留0.004cm、附著1秒、滴落厚度0.12cm。這些是共用設定，沒有角色卡片特例。

## 通解與形狀

遊戲命中是30Hz聚合液包，使用真正變形三角形的BVH球形掃掠；Niagara視覺粒子不逐粒子CPU trace，也不依賴PhysicsAsset提供皮膚UV。支援原生Gallery的PMC section0及可CPU讀取的SkeletalMesh LOD0，全部啟用材質分區納入；多人物各有獨立遮罩。

每個原始三角形配置獨立UV chart，依atlas預算每面4–64 texel；3454面為16×16／chart，2面PMC為64×64／chart。命中世界位置在動畫後表面畫柔邊圓形筆刷，分配量以有效筆刷面積正規化，重複噴射能累積。bilinear採樣與三角形外邊界擴張避免UV島讀到鄰島。原身體UV重疊不會讓其他部位意外沾漆。

材質從RT讀覆蓋率，白色與濕亮roughness保留底層人物材質。覆蓋幾何只複製原三角形，固定0.025cm偏移；不再按每面平均厚度擠出，避免接縫裂成小島。厚度感由濕亮材質與厚度梯度法線表現。單一大三角形内也能畫局部圓形，邊界不再沿整個三角形切割。

物理體積仍由120Hz有限體積三角形圖守恆。高解析遮罩依當前動畫世界法線反向追蹤重力切線，跨幾何共享邊取樣鄰chart，插值後按物理體積正規化。邊通量以遮罩邊界厚度／平均厚度調制：圓形污漬未碰共享邊時，不會只因面平均體積而憑空傳到鄰面。殘留薄膜及黏度控制滯留與流速；液滴按三角形＋重心座標附著，達附著時間才脫離，先扣體積，落地污漬依量決定大小。

## 最新驗證

- BuildR35 actual0（0464），BuildR37 actual0（0466）。BuildR36遭另一項正在建立的CrowdRoomEnvironment.inl尚未出現阻擋；檔案出現後重試通過，未補空檔或終止其它程序。先前LiveCoding／DLL鎖失敗保留E276及日誌。
- TestsR13 actual0，實際載入0466，Camera.KksMouse／Paint.Film／Paint.Scene三項全部Success。新增單一粗三角形的局部圓形遮罩、筆刷體積積分、動畫後遮罩非負／有限／正規化、跨UV接縫厚度一致，以及乾共享邊阻止輸送／沾濕後允許守恆輸送。原有兩人物獨立RT、GPU厚度讀回、清除、變形附著、拓撲更換、偏軸散射及30/60/120fps量守恆亦通過。
- VisualR12 actual0，實際載0464；與0466的噴漆程式相同，0466增加邊通量回歸及同場其它工作。先靜止噴射，再切動作，Painted／Flowing／Cleared均已檢視。圓形污漬及動畫附著已確認，清除無殘留。
- 1280×720、RTX5090、單個3454面人物、1024²雙RT、20Hz幾何更新：發射48.4cm³，最終film48.4、附著／地面0，守恆誤差小於1e-7。噴漆Actor CPU平均0.284ms、峰值23.091ms；4342引擎幀／11.998秒=361.881引擎幀/秒，是hidden Game的GFrameCounter／wall clock，不是GPU Present FPS或大型KKS卡效能承諾。
- 高面數歷史VisualR9有100800面、初始化369.118ms；負載與鏡頭不同，不提供倍數加速結論。原三角形斑塊baseline保留VisualR10Baseline，高面數資產保留TextureMaskAssetBackup，旧效果截圖／錯誤不覆寫。

## 範圍與限制

這是CPU守恆薄膜＋高解析渲染遮罩，未做完整FLIP、拉絲或分子凝聚。物理三角形圖仍較粗，atlas每面解析度依面數預算降低；不是無限細節污漬。RT／物理BVH按SurfaceHz（5–60，預設20）更新；覆蓋幾何及仍附著液滴每幀跟隨目前動畫，兩種更新頻率分開。不同mesh component未跨表面焊接；共享非流形邊封閉。單表面上限262144面、atlas最高2048²（雙RGBA16F約64MiB），3454面雙RT約16MiB，稀疏厚度資料只存沾漆面。

一般GPU SkeletalMesh為精確跟隨morph每幀GetCPUSkinnedVertices，可能flush，詳細卡片成本另測；本輪效能是無morph Poseable示範人物，PMC另有功能驗證。外部LOD0需保留CPU資料，不拿PhysicsAsset UV代替。現有CommonUI/GameViewportClient初始化Error仍存在；新輸入使用Slate preprocessor，未宣稱整份專案零錯誤。實體鍵鼠及正式封裝未冒充自動化測試。

證據：Saved/Tests/PaintSpray396，WatergunSnapshot含0466測試DLL與前後相同SHA256；錯誤／根因見ERROR_LOG E274–E276。
## 最新補強：局部滴落與遮罩歸零

滴落改從高解析遮罩選擇實際濕潤格：局部厚度達門檻，且位於朝下表面或沾濕的開放低邊，才生成液滴。生成點存真實重心座標，局部扣除格體積並同步扣Film帳目；不再以面平均厚度決定或一律從最低頂點滴落。更新外圍padding後仍守恆，殘留厚度不被扣掉。

表面量歸零時，HasRenderedPaint讓DrawFilm再執行一次，GPU寫入零遮罩並清空覆蓋幾何，避免最後一幀污漬留下。新增Blueprint API ClearPaintForActor：只清指定人物的表面及仍附著液滴，保留chart/BVH供再畫；其它人物、地面及已脫離的液滴保留。Cleared納入ConservationError帳目；R仍是全場清除。

BuildR38/39actual0；BuildR40被已開啟Editor的LiveCoding擋住，未關閉別人的視窗，改suffix建置BuildR41actual0。TestsR15實際0467、actual0，Camera.KksMouse／Paint.Film／Paint.Scene三項Success；新增乾角不滴、局部濕邊滴落（平均厚度未達門檻）、濕格扣量守恆、朝下PMC實際生成在沾漆區／附著後脫離、指定人物GPU coverage/thickness全零、另一人物量不變、再次噴射重用chart。測試前後DLL SHA256一致，WatergunSnapshot/WetDripFinal保存。未將這些結果冒充全部卡片／正式封裝測試。


## 每幀跟隨動畫

VisualR13暴露量仍存在卻看不到油漆的時序錯誤，已改成每幀更新覆蓋幾何、按SurfaceHz更新RT。PMC讀當前變形頂點；Poseable只蒙皮沾漆面所需頂點並快取共享頂點；一般Skinned使用精確CPU變形讀回。頂點數不變時使用UpdateMeshSection；沒有增加人物面數或把覆蓋層推遠。

新增低頻遮罩之間立即換姿勢的回歸：RT不換而覆蓋層已貼合新姿勢。VisualR14近景動作圖已確認污漬可見；CPU均值0.277ms、峰17.797ms（3454面Poseable，1024²、1280×720），不等於大型KKS卡保證。舊失敗照片保留VisualR13Backup，詳見E278。


最終程式R51實際編譯Sprayer及連結0477；全專案建置仍因其他引擎改動的NetCore.dll鎖定而actual6。Module-only空建置不計通過。隔離FrameFollowProject的TestsR17載入0477，actual0、Camera.KksMouse／Paint.Film／Paint.Scene全Success，包含骨架與非均勻縮放PMC子幀變形；DLL相同hash登錄正式專案，WatergunSnapshot保存新DLL、前後manifest與hash。每個paintable owner／component增加tick prerequisite，確保先完成角色變形再跟隨覆蓋層。完整引擎target、正式封裝與所有大型KKS卡仍未驗證。

正式專案VisualR15actual0、實際0477，靜止噴射／切彎腰動作／清除三張畫面均已檢視；48.8cm³守恆。噴漆Actor CPU平均0.253ms／峰17.907ms，4920引擎幀／12.048秒（GFrameCounter，非GPU Present FPS），測試前後DLL SHA256一致。證據及舊失敗照片均留存。


## KKS 讀卡的 3D 預覽

在角色卡清單選卡，按「加载3D」，模型載入成功後自動啟用水槍。將滑鼠移到中央3D區域，按住A再按住左鍵持續噴射；鬆開停止，R清除。上方「油漆設定」可展開調整速度、流量、散射、黏度及滴落。沒有按A時，沿用原本旋轉／平移／縮放。輸入搜尋文字、操作按鈕／滑桿不會噴射。

畫面透過SetModelLayout啟用共用Sprayer；返回卡片清單、开始换卡／换服装載入、widget結束均停射、清空油漆及清除按鍵狀態。重新打開3D重用同一個sprayer。面板只在3D顯示並位於Gallery上層。KKS以可見Part.Comp、SurfaceMat對應元件及MouthSlots的實際Comp/CpuComp（body／face／clothing morph替代）選表面，要求bRenderInMainPass；BodySkel palette載體與描邊不取作命中，透明表面的油漆sort priority沿用來源+1。沒有針對某張卡的頂點／材質特例。

新增ok.PaintGalleryAudit，透過實際Gallery SelectIndex／OnLoad3D執行KKS讀卡、噴射、動畫、返回清除、重開及再載入；環境變数OK_PAINT_CARD／ROOT／OUTPUT指定輸入與證據目錄。驗證結論以report.valid、命中量、畫面及實際載入DLL判斷，不以程序exit0代替。


### KKS 整合最終驗證

GalleryBuildR12完整target actual0／main0924。GalleryTestsR5實際0924、actual0，Camera.KksMouse／Paint.Film／Paint.Scene三項Success；新增離開3D停止噴射／清除／再次進入重用sprayer、故意錯誤renderer bounds仍命中、KKS morph display材質不同於原Part仍可沾漆且守恆。

GalleryAuditR10正式Template_Default＋實際GUI SelectIndex/OnLoad3D，KKS原卡[Card] March_7th_keiyuu.png，actual0／report valid1。95次命中／38cm³，3表面16638面，含Mouth_0原生morph body，白漆與動畫／返回清除／重開／再次讀卡全部流程通過，Painted／Animated／Cleared已檢視。測試瞄準裸露腿部、0.15°散射且補償重力供穩定回歸，玩家預設2°與實際彈道保持。此輪不宣稱已逐張測所有卡片或實體鍵鼠。

Skinned的候選範圍依當前變形頂點建立，命中沿用同一份頂點；不依render bounds猜皮膚位置。Morph body精確CPU讀回仍有成本：本例記錄單次update13.095ms／峰71.112ms，並非平均或FPS；不可把先前3454面無morph示範人物0.253ms當大型KKS卡效能。兩次冷載入及初始動畫目錄仍有等待，這次沒有宣称載入加速。DLL測試前後SHA256一致，snapshot保存GalleryIntegrationFinal0924及manifest，E279保存失敗／根因／修正。


## 多分區與隱藏狀態補強

一般OKPaintable程序網格支援全部可見材質分區，各分區有獨立atlas並以component＋section去重；原生KKS PMC仍排除重複描邊分區，角色面數不增加。已沾漆的人物、元件或PMC分區隱藏後，不再接收命中，覆蓋材質與仍附著液滴同步隱藏，表面流動／滴落暫停；重新顯示恢復既有污漬及命中能力。換卡／離開3D仍沿用全場清除。

MultiSectionBuildR2兩個修改單元及現有CrowdThreadPlacement編譯／LinkRecoveredR2成功0939；完整target未完成，不能把模組結果當全引擎建置。GalleryTestsR6正式專案實載0939、actual0，Camera.KksMouse／Paint.Film／Paint.Scene全Success，包含第2個PMC分區、分區與owner hide/show、覆蓋可見性、保留污漬／重用表面及守恆。

GalleryAuditR11真KKS March card正式讀卡3D流程valid1／actual0，94次命中37.6cm³、3表面16638面、動畫／返回清除／重開／重載通過。控制化裸腿瞄準／0.15°散射仍僅用於audit，玩家設定不改。單次update7.900ms、peak684.784ms，不能當平均或FPS，精確morph讀回與初始化仍有性能限制。DLL前後相同SHA256，MultiSectionBuildR2保存hash、來源及私有連結證據，WatergunSnapshot/MultiSectionFinal0939.dll保留實測版本；錯誤根因見E280。


2026-10-05後续通用性能優化與完整實測：見 [PAINT_PERFORMANCE282.md](PAINT_PERFORMANCE282.md)。正常新版DLL已建置且正式專案3項回歸通過。
