# E292 讀卡水槍與服裝資源（進行中）

水槍設定現在有「水柱不透明度」，0–1，獨立於命中流量和衣服染色。白色 film 在 toon 預覽燈光下可見，保留原 clothing material 與 lit 濕亮反射。

「遮罩解析度」512/1024/2048/4096，預設2048。變更後按 R 清除，再噴射會使用新精度；不增加人物面數。圖集每三角形的格數依模型面數分配，最大64，密集/超大模型的實際圖集尺寸與格數記錄於 Paint surface 日誌。

另存後服裝資源在背景驗證/复制，完成前禁止重複另存；畫面會顯示保存中，錯誤會明確提示，PNG 已寫入不代表衣服資源也已完成。

正式 APNG 原圖不重製。`.okstates` 補充原生半脫/移除的精確模型。新 OKPV0004 只保存差異，借用 hash 相同的原 APNG 及 IndependentResources。保存到另一資料夾時必須連同 state 資源，程式會自動執行。尚未準備的九件衣物組合仍須原始資源一次準備，不能宣稱所有組合離線。

批次工具：在專案根目錄執行 `pwsh -File Tools/prepare_reader_library.ps1 -Workers 4`。子程序強制 `OK_NOCACHE=1`，避免把每個原生模型與貼圖再存進 SQLite。完成才 valid=true。現正準備100卡所有outfit，保留原 PNG；報告包含逐 outfit 私有骨架及精確狀態 roundtrip。它不是全部動畫或每種服裝組合驗證。

Reader292BuildR15 全建置成功，1007 的四項 UE 回歸通過；材質 commandlet PaintBuild-20261005-114351 正常退出。通用流速預設24、黏度8。白色附著滴材質也使用 pigment emissive。

Reader292CompactFlow4KR1 使用無 verified receipt、空 gameRoot 的兩張 APNG 與 OKPV0004 精確服裝資源，實際 GUI 另存後仍可離線半脫及穿回。水柱1.0/0.35、衣服白色覆蓋、清除、busy callback及未改卡片的actor重用通過。停止5秒後film重心分別下降6.277cm與10.327cm，總量誤差小於1e-9；仍在飛行的水滴落到衣服後film量會增加，守恆應比較全系統。3840×2160截圖已檢查白色流痕與原衣服顏色，不能把輸出圖片尺寸當成每面mask格數。

截圖稽核等待 shader 編譯及結果服務完成。先前固定0.5秒/4幀仍得到黑畫面的 OtherCards4KR1/R2 是失敗案例；OtherCards4KR3 的 C002/C010/C050/C099 通過非黑屏檢查；這不代表完整材質與角色外觀符合原遊戲。

Reader292AllOutfitsIntegrityR2 通過100卡、720 outfit、6,593張不同貼圖的解碼、幾何/法線有限值、索引及私有骨架重複求值檢查；原PNG SHA與轉換receipt一致。這是全outfit資料完整性，不是720張逐張遊戲畫面比對。Reader292NativeOfflineR1的12個native/edited/offline/shape案例通過。全庫半脫/全脫1440個狀態的準備進度以 Saved/Tests/Reader292LibraryBatchR1/report.json 為準，未到 valid=true 不能宣稱完成。

快取與空間：已依使用者要求刪除173.87GiB的來源SQLite及伴檔，另刪164.15GiB未被manifest/程序引用的測試binary副本，卡片/dump/成品保留。Intermediate保留在D:/OpenKoikatsuWorking/Intermediate，以junction編譯。新的模組快照僅複製manifest所需DLL/uplugin/shaders，約29MiB。一般SQLite資料庫新增4GiB上限（OK_CACHE_MAX_MB可設64–16384MiB）；達上限不再存新blob，已有大型資料庫不會自動縮小，WAL可能短暫增加使用量。索引寫入失敗會回滾整筆更新，SQL accessor回傳None沿用原TSV/掃描fallback。SqlAtomicR3九項通過，NoSqlCacheR2的孤立掃描/TSV測試也通過。

批次續跑核對每卡 input_sha256，PNG已變更就重建，並在原圖被中途改動時停止。預設4個子程序；低於16GiB可用RAM不再增加解碼工作，磁碟最低3GiB。CLI prepare自身也停用SQLite，不依賴啟動器。Sideloader StripAll索引以完整模組清單path/size/mtime失效，只有一份程序內索引；讀取失敗不持久化不完整結果。manifest支援UTF-8 BOM、UTF-16LE/BE。MigrationCacheR2四項通過；實際庫此步驟首次1393ms、後續仍檢查inventory平均44.6ms，不能視為整次讀卡時間。SESSION_PIPELINE_VER251讓舊來源session重新解析正確GUID。

GPU全outfit測試入口：`pwsh -File Tools/run_reader_regression_audit.ps1 -Tag FreshTag -WornOnly -AllOutfits -CardIds "0,1,2,10,50,99" -CardDirectory C:/portable/OpenKoikatsu/output/CrowdAPNG`。只供已安裝隔離測試host/配對模組使用，日常啟動仍用CardReaderDevelopment.cmd。每outfit獨立PNG，必須等待report valid=1及image_checks=true，再人工檢視角色細節；兩項自動檢查不能取代外觀比對。

Reader292MultiOutfit4KR1 已完成6卡42套 GUI 切換與4K圖片，但人工檢查發現 C002 Alice 除outfit4外臉部特徵缺失，C050 outfit3/4手臂疑似缺失；尚未判定來源設定或匯入缺陷，不能宣稱完整視覺通過。Reader292SourceVisibility 四個 native-extractor/APNG 配對的全部 renderer 名稱、數量、可見性、面數、shader 完全一致；兩者共用解析邏輯，不能據此證明原遊戲也有問題。正在以原遊戲 Studio probe 比對。

原遊戲 Studio probe 已確認 Alice 的臉部與 Augusta outfit3 的手臂護甲正常存在。GPU Alice 缺五官、CPU 模式正常，因此正在修正 GPU 顯示路徑：形變資料改為不可序列化的 reference-space 快取，並保留替代表情模型的透明繪製排序。尚需新的實際 GPU 圖驗證，不能宣稱修復完成。


### 原生衣服動態遮罩修正（2026-10-05）
March 7th 腰部對稱白斑來自白色內衣 `_AlphaMask` 綁定遺失。對照使用者的原生 outfit0 dump 與 ChaControl 的 rendInner/texInnerAlphaMask 指派後，修正 source_program 分支：傳遞遮罩、UV ST 以及 alphaState，MainTex 保留，不移動或隱藏內衣。

- NativeMaskR1/R2：四種衣服狀態、PNG 方向/CPU 遮罩一致，主貼圖不改。
- WaistFixed4KR1：March 7th 7 outfit 真 GUI 3840×2160 有效，outfit0 白斑消失且白色領口/袖子保留。
- MaskRegressionR10：KksMouse/Film/MaskParity/Scene 四项成功。BuildR25/R26 成功。
- `ok-preview-pack repair-clothing-masks GAME INPUT OUTPUT`：單卡所有 outfit；`repair-clothing-mask-library GAME INPUT_DIRECTORY OUTPUT_DIRECTORY`：整庫來源 receipt SHA 驗證、保留 MainTex/幾何/影格、補入必要遮罩，輸出可離線 APNG。
- `Tools/verify_reader_mask_upgrade.py`：全庫非 okPv chunk 與尾端卡資料逐位元驗證。`Tools/publish_reader_mask_upgrade.ps1`：整庫驗證完成才可替換，原卡/receipt 備份，替換前再次 SHA 驗證。
- 全庫 staging: `Saved/Tests/Reader292MaskLibraryR1/Cards`。未 valid=true 前不可聲稱全部完成。新 payload 雜湊自然隔離舊 `.okstates`；須用新版 prepared-state 工具重新準備及驗證穿脫。

尚未解除的其他畫面疑點：Alice GPU face 與 Augusta 手臂材質；不能以這次腰部修正宣稱全部卡視覺忠實。


全库遮罩升级已发布：100卡/720outfit、128动态遮罩绑定、1127既有穿脱state已升级并验证模型几何不变。正式目录output/CrowdAPNG；原卡/receipt完整备份Saved/Backups/Reader292BeforeMaskUpgradeR1。全库非okPv chunks/尾端卡metadata逐位元不变，预览影格保留。

`upgrade-prepared-clothing-masks OLD_LIBRARY UPGRADED_LIBRARY` 使用新worn的遮罩绑定来升级旧真实穿脱模型，依每个state的top alphaState设置两通道，校验旧CRC/base hash、新编码roundtrip几何，避免重提取已正确模型。MaskRegressionR13四项成功；实际WaistUpgradedStates4KR1验证穿回/离线穿脱/水枪白色衣服流动有效。全库尚缺prepared states正在BatchR1补齐，未valid不得称1440全部完成。

E292 正式庫 GUI 驗證（2026-10-05）：Reader292WaistLive4KR1 已完成，valid=1、exit=0，三張 3840×2160 圖片均通過角色存在檢查，人工檢視 C001_Worn.png 確認腰部白斑消失，白色內衣領口／袖子保留。正式 output/CrowdAPNG 使用 normal 1017；真實半脫 8741 面、穿著 14832 面、全脫衣物 0 面、穿回與另存後離線狀態通過。水柱透明度 0.35、衣服 coating=1，5 秒流痕重心下降 8.015 cm、守恆誤差約零。此測試不代表 Alice GPU face／Augusta 手臂材質已修復。

### E292 全庫遮罩與穿脫資源完成（2026-10-05，最終檢查點）

- Reader292LibraryBatchR1/report.json：valid=true、100 卡、720 outfit、1440 states、png_unchanged=true、errors=[]；批次程序正常 exit=0。live-hash-validation.json 再次核對全部 100 張正式 PNG、verified receipt、prepared report 的 SHA256 相符；720 outfit 的 private rig/finite 與 1440 幾何 roundtrip 全部通過。
- 正式庫維持 output/CrowdAPNG，CardReaderDevelopment.cmd 直接使用。100 張 APNG 的所有 outfit 已遷移必要衣服遮罩，原預覽影格與尾端卡資料未改；原卡與 receipt 保留於 Saved/Backups/Reader292BeforeMaskUpgradeR1。所有目標半脫/全脫快照已備妥，可由相同 APNG 資源離線讀取，不需首次重新拆原生控制器/衣物。
- 狀態驗證範圍為每 outfit 的 111100000 與 333300000（上衣、下裝、胸罩、內褲），並非九欄所有 4^9 組合。720 outfit 是資料完整性/骨架/幾何驗證，不是逐張原遊戲視覺比對。
- 最終建置 Reader292BuildR28 成功；normal main=1017，ModuleSnapshots/1017 匹配依賴。MaskRegressionR13 的 KksMouse/Film/MaskParity/Scene 四項 Success，NativeMaskR2 四衣服狀態的動態 mask/UV/ST/MainTex 契約通過。
- 正式庫 Reader292WaistLive4KR1：valid=1、exit=0，4K 截圖已檢視，March 7th 腰部白斑消失，白色領口/袖子保留；穿脫/穿回/另存離線、水槍 opacity .35、衣服流痕 5 秒下落 8.015cm、質量守恆通過。
- 獨立未解問題仍為 Alice GPU face 與 Augusta 部分手臂材質；不得用本輪遮罩/狀態驗證宣稱整個專案零 bug。不得清除 concurrent Crowd 工作或原卡/dump/有效 resources。
