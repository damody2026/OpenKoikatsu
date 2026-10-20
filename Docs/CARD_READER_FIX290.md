# 讀卡畫面三項修正 E290（2026-10-05）

後續 E291 已改用內嵌資源 SHA256 並自動攜帶另存卡的已準備狀態；舊整卡雜湊快取仍相容。最新細節見 [CARD_READER_FIX291.md](CARD_READER_FIX291.md)。

重新啟動 `CardReaderDevelopment.cmd`。APNG 預設目錄維持 `output/CrowdAPNG` 的 100 張正式卡；載入 3D 後，按住 A，再在人物畫面按住左鍵噴射，R 清除。衣服區可用穿上／半脫／全脫，或點各衣物切換狀態。

## 根因及通用修正

1. 水槍：`SetupStudioView` 會隱藏場景中的其它 Actor，重用的 Sprayer 也被隱藏；因此 Niagara active、命中量正常仍沒有水柱。互動工具標記 `OKPreviewInteraction`，預覽清場保留它。A 的 Slate 與 PlayerController 按下／釋放路徑一致，搜尋輸入不啟用水槍，UI 起始左鍵不取得噴射權。槍口偏離視線並向人物 `OrbitFocus` 深度收斂；共用粒子材質改白色 unlit emissive，保留現有水柱密度、物理與材質遮罩算法。
2. 頭髮：APNG 的 private rig 部件可能保存 palette 模式下的 prefab 局部頂點，但讀卡的 procedural 顯示需要求值後的角色空間頂點。每次 portable 載入以原有骨骼／父節點／縮放／DynamicBone 路徑重新計算 private rig，時間步為零。不移動特定卡片、不添加固定頭髮位移、不增加面數。
3. 衣服：同卡重用鍵原本沒有九個衣服狀態，按鈕改了文字仍重用穿著角色；舊 APNG 又只保存匯出時保留的 prefab 節點，不能從缺失幾何造出半脫。現在重用鍵包含全部九狀態。舊卡首次切換缺失狀態時，核對 verified receipt 的 APNG／來源 SHA256，以原有 KKS 原生讀取流程（StateType、連動衣物、prefab 選取與 body mask）建立真正的狀態快照。解碼及 CRC 檢查後原子發布，後續直接離線讀取；不以任意隱藏三角形偽造半脫。載入失敗還原衣服標籤，保留原角色。

## 舊 APNG 的必要資源

首次準備尚未存在的衣服狀態，需同名 `.verified.json`、其中記錄的原始卡及其遊戲資源。狀態快照位於卡片目錄 `.okstates/<APNG SHA256>/outfitN_<9個狀態數字>.okpreview`。複製 APNG 給其它機器時需連同 `.okstates` 及專案共用資源；已準備狀態無需遊戲本體或 receipt。不是所有舊 APNG 已內嵌所有半脫模型；缺少來源時明確回報缺資源，不假裝成功。

目前 C000、C001 的 outfit0 已準備半脫、全脫及混合狀態。其它卡／服裝按需準備。原 APNG 和來源卡未修改。

## 驗證

- UE 實際版本 5.8.3；`Reader290BuildR10.log` 模組建置成功，包含 KKUnityBundle 與 OpenKoikatsu，正常模組配對 0997。
- `Saved/Tests/Reader290LibraryR5.log` 與 `Reader290Library.json`：100 張 outfit0 載入；1977 個 private rig 部件頂點有限，再次零時間求值差異為零。March C001 的 24 個 hair/kami 部件最低高度 1.325279m，沒有落地。
- C000／C001 各三種狀態，共六案例：衣物／身體節點、可見性、索引、UV、body mask 與原生載入逐項一致；只拷 APNG 和狀態快照到無 receipt 目錄，六案均能離線讀取。
- `Saved/Tests/PaintSpray396/Reader290RegressionR2`：Camera.KksMouse、Paint.Film、Paint.MaskParity、Paint.Scene 四項 Success，exit0。
- `Saved/Tests/Reader290GuiR5`：真正 Gallery 依序穿著→噴射→半脫→全脫→穿著，檢查 Actor 可見、Niagara ready／active、命中量、實際可見衣物三角形及恢復。穿著／噴射截圖人工檢視，兩張頭髮皆在頭部，連續白色水柱可見。數值切換驗證不輸出裸身截圖。
- 不把引擎合成鍵盤事件稱為實體鍵鼠測試，不宣稱全部 outfit／衣物組合已測完，也沒有用本輪功能測試宣稱 FPS 改善。

## 重跑

Rust：`cargo test --manifest-path Tools/ok-extract/Cargo.toml --release --lib prepared_library_hair_and_clothing_states -- --ignored --test-threads=1 --nocapture`，此測試持有程序環境，需本機正式庫及原始來源。

UE：匹配的 `ModuleSnapshots/0997` 配合 `Tools/run_paint_performance_audit.ps1 -Tag <新名稱> -Suffix 0997 -Regression`；再依序 `Tools/run_reader_regression_audit.ps1 -Tag <新名稱>`。後者使用已準備的 Reader290Offline fixture 與前者更新的獨立 audit host，不與其它 GPU audit 並行。
